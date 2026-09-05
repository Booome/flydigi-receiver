/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * default: BT HID host connect + raw HID report capture.
 *
 * 三层候选算法（spec §四）：
 *   层1 语义过滤：只保留 gamepad-class BR/EDR（COD major=5 minor=2）
 *   层2 EWMA 平滑：0.3*新 + 0.7*旧，去除 ±3~5dB RSSI 抖动
 *   层3 迟滞 + 兜底：3dB 迟滞，3s 稳定或 8s 强制连
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_hid_gap.h"
#include "esp_hidh.h"
#include "esp_hidh_gattc.h"
#include "hid_report.h"

#define TAG "default"
#define SCAN_DURATION_SEC 3
#define HYSTERESIS_DB 3
#define LOCK_WAIT_MS 3000
#define MAX_WAIT_MS 8000
#define CONNECT_TIMEOUT_MS                                                                         \
    4000 /* open() issued but no OPEN/CLOSE event for this long -> give up, rescan */
#define RETRY_BACKOFF_MS 300 /* pause after a failed/disconnected attempt before rescanning */
#define EWMA_ALPHA_NUM 3     /* /10 → alpha=0.3 */
#define EWMA_ALPHA_DEN 10
#define CANDIDATE_GONE_ROUNDS 2
#define EWMA_MAX 16

/* Per-device EWMA RSSI: key = (bda, transport) */
typedef struct {
    uint8_t bda[6];
    esp_hid_transport_t transport;
    bool used;
    float smoothed;
} ewma_entry_t;

/* Candidate state */
typedef struct {
    bool active;
    uint8_t bda[6];
    esp_hid_transport_t transport;
    float smoothed;
    int64_t set_at_ms;
} candidate_t;

/* Connection lifecycle. Only ST_SCANNING may scan or issue a new open;
 * ST_CONNECTING/CONNECTED must NOT run inquiry (it disturbs the ACL link)
 * nor re-open. Events drive SCANNING<->CONNECTING<->CONNECTED. */
typedef enum { ST_SCANNING, ST_CONNECTING, ST_CONNECTED } app_state_t;

static ewma_entry_t g_ewma[EWMA_MAX];
static candidate_t g_candidate = {0};
static int g_not_seen_count = 0;
static int64_t g_scan_start_ms = 0;
static volatile app_state_t g_state = ST_SCANNING;
static int64_t g_state_since_ms = 0;
static int64_t g_rescan_at_ms = 0; /* backoff gate before next scan after fail/close */

static const char *transport_str(esp_hid_transport_t t) {
    switch (t) {
    case ESP_HID_TRANSPORT_BLE:
        return "BLE";
    case ESP_HID_TRANSPORT_BT:
        return "BR_EDR";
    default:
        return "?";
    }
}

static void print_bda(const uint8_t *bda) {
    printf("%02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static uint8_t bda_eq(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 6) == 0;
}

/* Known Apex5 BT identities (BR/EDR advertised name) seen so far. The pad
 * presents a different name per its onboard connection mode, and unknown new
 * names are possible — extend this list as they appear. */
static const char *const g_gamepad_names[] = {
    "Xbox Wireless Controller", /* PC>Bluetooth / Android / iOS = X-input */
    "Pro Controller",           /* Nintendo Switch (NS) mode             */
};

static bool is_gamepad(const esp_hid_scan_result_t *r) {
    /* Match by BT transport + name in the known-identity allowlist
     * (case-insensitive). Add new names to g_gamepad_names as seen. */
    if (r->transport != ESP_HID_TRANSPORT_BT)
        return false;
    if (!r->name)
        return false;
    for (size_t i = 0; i < sizeof(g_gamepad_names) / sizeof(g_gamepad_names[0]); i++) {
        if (strcasecmp(r->name, g_gamepad_names[i]) == 0)
            return true;
    }
    return false;
}

static float *ewma_get_or_create(const uint8_t *bda, esp_hid_transport_t t) {
    for (int i = 0; i < EWMA_MAX; i++) {
        if (g_ewma[i].used && bda_eq(g_ewma[i].bda, bda) && g_ewma[i].transport == t) {
            return &g_ewma[i].smoothed;
        }
    }
    for (int i = 0; i < EWMA_MAX; i++) {
        if (!g_ewma[i].used) {
            memcpy(g_ewma[i].bda, bda, 6);
            g_ewma[i].transport = t;
            g_ewma[i].used = true;
            g_ewma[i].smoothed = -127.0f;
            return &g_ewma[i].smoothed;
        }
    }
    return NULL;
}

static void ewma_clear(void) {
    for (int i = 0; i < EWMA_MAX; i++)
        g_ewma[i].used = false;
}

static void set_state(app_state_t s, int64_t now_ms) {
    g_state = s;
    g_state_since_ms = now_ms;
}

/* Go back to scanning: clear candidate/ewma, restart the lock clock. */
static void reset_to_scan(void) {
    memset(&g_candidate, 0, sizeof(g_candidate));
    ewma_clear();
    g_not_seen_count = 0;
    g_scan_start_ms = esp_timer_get_time() / 1000;
    set_state(ST_SCANNING, g_scan_start_ms);
}

/* Called after a failed open or a close: rescan, but back off briefly so we
 * don't hammer the controller while it is still in a transient state. */
static void backoff_to_scan(void) {
    reset_to_scan();
    g_rescan_at_ms = esp_timer_get_time() / 1000 + RETRY_BACKOFF_MS;
}

/* Track consecutive connect failures to one address. After a few, the cause is
 * usually a stale bond on the PAD side (which a host cannot clear remotely), so
 * surface an actionable hint and back off longer instead of silently thrashing.
 * Cleared on a successful connect. */
#define CONNECT_FAIL_HINT_AFTER 3
static esp_bd_addr_t g_fail_bda = {0};
static int g_fail_count = 0;

static void note_connect_fail(const uint8_t *bda) {
    if (bda && memcmp(bda, g_fail_bda, sizeof(esp_bd_addr_t)) == 0) {
        g_fail_count++;
    } else {
        memcpy(g_fail_bda, bda ? bda : g_fail_bda, sizeof(esp_bd_addr_t));
        g_fail_count = 1;
    }
    if (g_fail_count == CONNECT_FAIL_HINT_AFTER) {
        printf("[hid] %d connect fails to ", g_fail_count);
        print_bda(g_fail_bda);
        printf(" - pad may hold a stale bond, re-enter PAIRING on the pad\n");
        g_rescan_at_ms = esp_timer_get_time() / 1000 + RETRY_BACKOFF_MS * 4;
    }
}

static void note_connect_ok(const uint8_t *bda) {
    (void)bda;
    g_fail_count = 0;
    memset(g_fail_bda, 0, sizeof(g_fail_bda));
}

/* Drop our stored link key for a peer. Needed when the pad re-pairs (it forgot
 * us) but we keep an old key: esp_hidh_dev_open then attempts an ENCRYPTED
 * reconnect the repaired pad won't honor, so it hangs until timeout with no
 * OPEN event -> without clearing here too, we retry the same stale key forever. */
static void remove_bond_of(const uint8_t *bda) {
    if (!bda) {
        return;
    }
    esp_bd_addr_t peer;
    memcpy(peer, bda, sizeof(esp_bd_addr_t));
    esp_bt_gap_remove_bond_device(peer);
    printf("[hid] bond removed for ");
    print_bda(bda);
    printf(" (bond_num=%d)\n", esp_bt_gap_get_bond_device_num());
}

static void
hidh_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;
    const uint8_t *bda = NULL;
    esp_hid_transport_t transport = ESP_HID_TRANSPORT_BT;

    switch (event_id) {
    case ESP_HIDH_OPEN_EVENT:
        bda = esp_hidh_dev_bda_get(param->open.dev);
        transport = esp_hidh_dev_transport_get(param->open.dev);
        if (param->open.status == ESP_OK) {
            set_state(ST_CONNECTED, esp_timer_get_time() / 1000);
            printf("[hid] open: addr=");
            print_bda(bda);
            printf(" transport=%s\n", transport_str(transport));
            hid_dump_report_map(param->open.dev);
            note_connect_ok(bda);
        } else {
            printf("[hid] open FAIL: addr=");
            print_bda(bda);
            printf(
                " transport=%s status=0x%x\n",
                transport_str(transport),
                (unsigned)param->open.status
            );
            /* Stale bond on our side vs a re-paired pad -> auth fails. Free the
             * dev first, then drop the link key so the next scan+open does a
             * fresh Just Works pairing instead of repeating the failure. */
            if (param->open.dev) {
                esp_hidh_dev_free(param->open.dev);
            }
            remove_bond_of(bda);
            backoff_to_scan();
            note_connect_fail(bda); /* may lengthen backoff + hint after a few */
        }
        break;
    case ESP_HIDH_INPUT_EVENT: {
        apex5_xinput_t cur;
        if (!hid_decode(param->input.data, param->input.length, &cur)) {
            break;
        }
#if HID_DEBUG_DELTA
        /* bring-up aid: show which raw bytes moved, to map fields in Task 3 */
        static uint8_t raw_prev[64];
        static uint16_t raw_prev_len = 0;
        if (param->input.length <= sizeof(raw_prev)) {
            for (uint16_t i = 0; i < param->input.length; i++) {
                if (i >= raw_prev_len || raw_prev[i] != param->input.data[i]) {
                    printf(
                        "[hid] d b%u:%02x>%02x ",
                        i,
                        i < raw_prev_len ? raw_prev[i] : 0,
                        param->input.data[i]
                    );
                }
            }
            printf("\n");
            memcpy(raw_prev, param->input.data, param->input.length);
            raw_prev_len = param->input.length;
        }
#endif
        static apex5_xinput_t prev;
        static bool have_prev = false;
        if (!have_prev || memcmp(&prev, &cur, sizeof(cur)) != 0) {
            hid_print_state(&cur);
            prev = cur;
            have_prev = true;
        }
        break;
    }
    case ESP_HIDH_CLOSE_EVENT:
        bda = esp_hidh_dev_bda_get(param->close.dev);
        transport = esp_hidh_dev_transport_get(param->close.dev);
        printf("[hid] close: addr=");
        print_bda(bda);
        printf(
            " transport=%s status=0x%x\n", transport_str(transport), (unsigned)param->close.status
        );
        if (param->close.dev) {
            esp_hidh_dev_free(param->close.dev);
        }
        backoff_to_scan();
        break;
    default:
        break;
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_hid_gap_init(ESP_BT_MODE_BTDM));
    ESP_LOGI(TAG, "BTDM initialized");

#if CONFIG_BT_BLE_ENABLED
    /* esp_hid_scan() blocks on a BLE semaphore; need GATTC callback registered */
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler));
#endif

    esp_hidh_config_t hidh_cfg = {
        .callback = hidh_event_handler,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(esp_hidh_init(&hidh_cfg));

    /* Diagnostic: what BR/EDR links keys do we already have at boot? A stale
     * entry for the pad is what makes a repaired pad fail to re-pair quickly. */
    {
        int nb = esp_bt_gap_get_bond_device_num();
        printf("[hid] boot bonds=%d", nb);
        if (nb > 0) {
            esp_bd_addr_t list[8];
            int want = nb > 8 ? 8 : nb;
            if (esp_bt_gap_get_bond_device_list(&want, list) == ESP_OK) {
                for (int i = 0; i < want; i++) {
                    printf(" ");
                    print_bda(list[i]);
                }
                /* Root cause of "pad stuck 连接中 / pairing while we idle in
                 * inquiry": the pad's bonded reconnect state is page-scannable
                 * but not inquiry-scannable. Our scan-only loop would never
                 * find it. If we have any bonded BR/EDR peer, seed an
                 * OUTBOUND page now so we can (a) reconnect a matching pad and
                 * (b) on auth failure hit the timeout/fail path that clears
                 * the stale bond, then fall back to inquiry for fresh pairing.
                 */
                memcpy(g_candidate.bda, list[0], sizeof(esp_bd_addr_t));
                g_candidate.transport = ESP_HID_TRANSPORT_BT;
                g_candidate.smoothed = 0;
                g_candidate.active = true;
                int64_t t = esp_timer_get_time() / 1000;
                g_candidate.set_at_ms = t;
                set_state(ST_CONNECTING, t);
                printf("\n[hid] boot: outbound page bonded ");
                print_bda(g_candidate.bda);
                printf("\n");
                esp_hidh_dev_open(g_candidate.bda, ESP_HID_TRANSPORT_BT, 0);
            }
        }
        printf("\n");
    }

    g_scan_start_ms = esp_timer_get_time() / 1000;

    while (1) {
        int64_t now = esp_timer_get_time() / 1000;

        /* CONNECTED: do not scan or re-open (inquiry would break the link).
         * Reports arrive via the event callback. Wait for a close event. */
        if (g_state == ST_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        /* CONNECTING: a pending open owns the radio; wait for its result,
         * but bail out to rescan if no event arrives (device vanished). */
        if (g_state == ST_CONNECTING) {
            if (now - g_state_since_ms > CONNECT_TIMEOUT_MS) {
                printf("[hid] connect timeout, rescan\n");
                esp_bd_addr_t tried;
                memcpy(tried, g_candidate.bda, sizeof(tried));
                reset_to_scan();
                /* A hung attempt (no OPEN event) usually means a stale link key
                 * on our side vs a re-paired pad: clear it so the next try does
                 * fresh Just Works instead of a doomed encrypted reconnect. */
                remove_bond_of(tried);
                note_connect_fail(tried);
            } else {
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
        }
        /* ST_SCANNING but still inside a post-fail/backoff window: idle. */
        if (now < g_rescan_at_ms) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        esp_hid_scan_result_t *results = NULL;
        size_t n = 0;
        esp_err_t sr = esp_hid_scan(SCAN_DURATION_SEC, &n, &results);
        if (sr != ESP_OK) {
            ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(sr));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* 层1: 只保留 gamepad-class BR/EDR; count distinct gamepads for
         * single-candidate fast-connect (no competitor -> no need to wait for
         * the 3s anti-oscillation window). */
        esp_hid_scan_result_t *best = NULL;
        int gp_count = 0;
        for (size_t i = 0; i < n; i++) {
            if (is_gamepad(&results[i])) {
                gp_count++;
                if (!best || results[i].rssi > best->rssi) {
                    best = &results[i];
                }
            }
        }

        if (!best) {
            if (g_candidate.active && g_not_seen_count >= CANDIDATE_GONE_ROUNDS) {
                printf("[hid] candidate gone\n");
                memset(&g_candidate, 0, sizeof(g_candidate));
            }
            if (g_candidate.active)
                g_not_seen_count++;
            esp_hid_scan_results_free(results);
            continue;
        }

        /* 层2: EWMA 平滑 */
        float *smoothed_p = ewma_get_or_create(best->bda, best->transport);
        if (!smoothed_p) {
            esp_hid_scan_results_free(results);
            continue;
        }
        if (*smoothed_p == -127.0f) {
            *smoothed_p = (float)best->rssi; /* 首次采到 */
        } else {
            *smoothed_p = (float)best->rssi * EWMA_ALPHA_NUM / EWMA_ALPHA_DEN +
                          *smoothed_p * (EWMA_ALPHA_DEN - EWMA_ALPHA_NUM) / EWMA_ALPHA_DEN;
        }

        /* 层3: 迟滞 + 锁定计时 */
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (!g_candidate.active) {
            g_candidate.active = true;
            memcpy(g_candidate.bda, best->bda, 6);
            g_candidate.transport = best->transport;
            g_candidate.smoothed = *smoothed_p;
            g_candidate.set_at_ms = now_ms;
            g_not_seen_count = 0;
            printf("[hid] candidate: addr=");
            print_bda(best->bda);
            printf(" smoothed=%.1f gp=%d\n", g_candidate.smoothed, gp_count);
        } else if (bda_eq(best->bda, g_candidate.bda) && best->transport == g_candidate.transport) {
            /* 同候选: 计时继续朝 LOCK_WAIT_MS 走 */
            g_not_seen_count = 0;
        } else if (*smoothed_p >= g_candidate.smoothed + HYSTERESIS_DB) {
            /* 新候选平滑 RSSI 比候选大 >= 3dB → 明显更强; 替换 + 重置计时 */
            g_candidate.active = true;
            memcpy(g_candidate.bda, best->bda, 6);
            g_candidate.transport = best->transport;
            g_candidate.smoothed = *smoothed_p;
            g_candidate.set_at_ms = now_ms;
            g_not_seen_count = 0;
            printf("[hid] candidate replace: addr=");
            print_bda(best->bda);
            printf(" smoothed=%.1f\n", g_candidate.smoothed);
        }
        /* else: 接近打平, 保留, 不重置计时 */

        /* 锁定条件: 稳定 3s 或超 8s 兜底 -> 发起连接并转入 CONNECTING。
         * 不再 reset_to_scan：连接结果由 OPEN/CLOSE 事件驱动状态迁移。 */
        int64_t stable_elapsed = now_ms - g_candidate.set_at_ms;
        int64_t total_elapsed = now_ms - g_scan_start_ms;
        if (stable_elapsed >= LOCK_WAIT_MS || total_elapsed >= MAX_WAIT_MS) {
            printf("[hid] connecting: addr=");
            print_bda(g_candidate.bda);
            printf(" transport=%s\n", transport_str(g_candidate.transport));
            set_state(ST_CONNECTING, now_ms);
            esp_hidh_dev_open(g_candidate.bda, g_candidate.transport, 0);
        }

        esp_hid_scan_results_free(results);
    }
}