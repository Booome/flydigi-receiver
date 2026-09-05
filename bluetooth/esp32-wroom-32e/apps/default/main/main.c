/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * default: BT HID host, event/callback-driven (no polling loop).
 *
 * app_main brings the stack up, registers the GAP + HID callbacks + timers,
 * makes ONE start decision (page a bonded peer, else continuous inquiry), then
 * returns. Thereafter every transition is driven by GAP DISC_RES/DISC_STATE,
 * HID OPEN/INPUT/CLOSE, and three esp_timer ticks. No polling loop, no blocking scan.
 *
 * Selection (unchanged 3-layer): gamepad name allowlist -> EWMA(0.3) ->
 * hysteresis 3dB -> lock when stable>=3s or total>=8s. Re-evaluated on a 250ms
 * tick so a quiet link still locks on schedule. See
 * docs/superpowers/specs/2026-09-06-bt-hid-callback-refactor-design.md
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh.h"
#include "bt_stack.h"
#include "hid_report.h"

#define TAG "default"

/* Windows/constants unchanged from the polling implementation. */
#define HYSTERESIS_DB 3
#define LOCK_WAIT_MS 3000
#define MAX_WAIT_MS 8000
#define CONNECT_TIMEOUT_MS 4000 /* open() with no OPEN/CLOSE event -> give up */
#define RETRY_BACKOFF_MS 300    /* pause after a fail/close before resuming */
#define CANDIDATE_GONE_MS 6000  /* candidate not sighted this long -> drop it */
#define LOCK_TICK_MS 250        /* periodic re-evaluation cadence */
#define INQ_LENGTH 8            /* esp_bt_gap_start_discovery length (8*1.28s) */
#define EWMA_ALPHA_NUM 3        /* /10 -> alpha=0.3 */
#define EWMA_ALPHA_DEN 10
#define EWMA_MAX 16
#define CONNECT_FAIL_HINT_AFTER 3

typedef struct {
    uint8_t bda[6];
    bool used;
    float smoothed;
    int64_t last_seen_ms;
} ewma_entry_t;

typedef struct {
    bool active;
    uint8_t bda[6];
    float smoothed;
    int64_t set_at_ms;
} candidate_t;

/* Guard only (which side-effects are allowed); transitions are event-driven. */
typedef enum { ST_SCANNING, ST_CONNECTING, ST_CONNECTED } app_state_t;

static ewma_entry_t g_ewma[EWMA_MAX];
static candidate_t g_candidate = {0};
static int64_t g_scan_start_ms = 0;
static volatile app_state_t g_state = ST_SCANNING;
static esp_bd_addr_t g_fail_bda = {0};
static int g_fail_count = 0;
static SemaphoreHandle_t g_app_mutex = NULL;

static esp_timer_handle_t g_lock_tick = NULL;      /* periodic, SCANNING only */
static esp_timer_handle_t g_conn_timeout = NULL;   /* one-shot, armed on open */
static esp_timer_handle_t g_rescan_backoff = NULL; /* one-shot */

/* Known Apex5 BT identities (BR/EDR advertised name). Extend as new names appear. */
static const char *const g_gamepad_names[] = {
    "Xbox Wireless Controller", /* PC>Bluetooth / Android / iOS = X-input */
    "Pro Controller",           /* Nintendo Switch (NS) mode             */
};

static void lock(void) {
    xSemaphoreTake(g_app_mutex, portMAX_DELAY);
}
static void unlock(void) {
    xSemaphoreGive(g_app_mutex);
}

static int64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

static void print_bda(const uint8_t *bda) {
    printf("%02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static uint8_t bda_eq(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 6) == 0;
}

static bool is_gamepad(const char *name) {
    if (!name) {
        return false;
    }
    for (size_t i = 0; i < sizeof(g_gamepad_names) / sizeof(g_gamepad_names[0]); i++) {
        if (strcasecmp(name, g_gamepad_names[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void ewma_clear(void) {
    for (int i = 0; i < EWMA_MAX; i++) {
        g_ewma[i].used = false;
    }
}

static ewma_entry_t *ewma_find(const uint8_t *bda) {
    for (int i = 0; i < EWMA_MAX; i++) {
        if (g_ewma[i].used && bda_eq(g_ewma[i].bda, bda)) {
            return &g_ewma[i];
        }
    }
    return NULL;
}

static ewma_entry_t *ewma_get_or_create(const uint8_t *bda) {
    ewma_entry_t *e = ewma_find(bda);
    if (e) {
        return e;
    }
    for (int i = 0; i < EWMA_MAX; i++) {
        if (!g_ewma[i].used) {
            memcpy(g_ewma[i].bda, bda, 6);
            g_ewma[i].used = true;
            g_ewma[i].smoothed = -127.0f;
            return &g_ewma[i];
        }
    }
    return NULL;
}

/* 层2 EWMA + record sighting time. Returns the smoothed value. */
static float ewma_update(const uint8_t *bda, int8_t rssi, int64_t t) {
    ewma_entry_t *e = ewma_get_or_create(bda);
    if (!e) {
        return -127.0f;
    }
    if (e->smoothed == -127.0f) {
        e->smoothed = (float)rssi;
    } else {
        e->smoothed = (float)rssi * EWMA_ALPHA_NUM / EWMA_ALPHA_DEN +
                      e->smoothed * (EWMA_ALPHA_DEN - EWMA_ALPHA_NUM) / EWMA_ALPHA_DEN;
    }
    e->last_seen_ms = t;
    return e->smoothed;
}

/* 层3 迟滞 + 候选计时 (no lock decision here; lock_tick decides). */
static void candidate_update(const uint8_t *bda, float smoothed, int64_t t) {
    if (!g_candidate.active) {
        g_candidate.active = true;
        memcpy(g_candidate.bda, bda, 6);
        g_candidate.smoothed = smoothed;
        g_candidate.set_at_ms = t;
        printf("[hid] candidate: addr=");
        print_bda(bda);
        printf(" smoothed=%.1f\n", smoothed);
    } else if (bda_eq(bda, g_candidate.bda)) {
        /* same candidate; lock clock keeps running */
    } else if (smoothed >= g_candidate.smoothed + HYSTERESIS_DB) {
        g_candidate.active = true;
        memcpy(g_candidate.bda, bda, 6);
        g_candidate.smoothed = smoothed;
        g_candidate.set_at_ms = t;
        printf("[hid] candidate replace: addr=");
        print_bda(bda);
        printf(" smoothed=%.1f\n", smoothed);
    }
    /* else: near-tie, keep existing candidate + clock */
}

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

/* Count consecutive silent failures to one addr; clear a hopeless bond after N.
 * Returns the backoff delay (ms) to use for this failure. Caller holds lock. */
static int64_t note_connect_fail(const uint8_t *bda) {
    if (bda && memcmp(bda, g_fail_bda, sizeof(esp_bd_addr_t)) == 0) {
        g_fail_count++;
    } else {
        memcpy(g_fail_bda, bda ? bda : g_fail_bda, sizeof(esp_bd_addr_t));
        g_fail_count = 1;
    }
    if (g_fail_count == CONNECT_FAIL_HINT_AFTER) {
        printf("[hid] %d connect fails to ", g_fail_count);
        print_bda(g_fail_bda);
        printf(" - dropping bond, re-pair if needed\n");
        remove_bond_of(g_fail_bda);
        return RETRY_BACKOFF_MS * 4;
    }
    return RETRY_BACKOFF_MS;
}

static void note_connect_ok(const uint8_t *bda) {
    (void)bda;
    g_fail_count = 0;
    memset(g_fail_bda, 0, sizeof(g_fail_bda));
}

/* ---- side-effect helpers (all called with lock held) ---- */

static void arm_rescan(int64_t delay_ms) {
    esp_timer_start_once(g_rescan_backoff, (uint64_t)delay_ms * 1000);
}

static void begin_scan_round(void) {
    memset(&g_candidate, 0, sizeof(g_candidate));
    ewma_clear();
    g_scan_start_ms = now_ms();
    g_state = ST_SCANNING;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, INQ_LENGTH, 0);
    esp_timer_start_periodic(g_lock_tick, (uint64_t)LOCK_TICK_MS * 1000);
}

/* stop scanning side-effects before we take the radio for an open. */
static void halt_scanning_side_effects(void) {
    esp_bt_gap_cancel_discovery();
    esp_timer_stop(g_lock_tick);
}

static void open_candidate(void) {
    halt_scanning_side_effects();
    esp_timer_start_once(g_conn_timeout, (uint64_t)CONNECT_TIMEOUT_MS * 1000);
    g_state = ST_CONNECTING;
    printf("[hid] connecting: addr=");
    print_bda(g_candidate.bda);
    printf(" transport=BR_EDR\n");
    esp_hidh_dev_open(g_candidate.bda, ESP_HID_TRANSPORT_BT, 0);
}

static void open_bonded(void) {
    int nb = esp_bt_gap_get_bond_device_num();
    if (nb <= 0) {
        return;
    }
    esp_bd_addr_t list[8];
    int want = nb > 8 ? 8 : nb;
    if (esp_bt_gap_get_bond_device_list(&want, list) != ESP_OK || want <= 0) {
        return;
    }
    memcpy(g_candidate.bda, list[0], sizeof(esp_bd_addr_t));
    g_candidate.smoothed = 0;
    g_candidate.active = true;
    g_candidate.set_at_ms = now_ms();
    halt_scanning_side_effects();
    esp_timer_start_once(g_conn_timeout, (uint64_t)CONNECT_TIMEOUT_MS * 1000);
    g_state = ST_CONNECTING;
    printf("[hid] outbound page bonded ");
    print_bda(g_candidate.bda);
    printf(" (bonds=%d)\n", want);
    esp_hidh_dev_open(g_candidate.bda, ESP_HID_TRANSPORT_BT, 0);
}

/* Resume after boot / fail / close: page the bonded peer if any, else scan. */
static void resume_scan(void) {
    if (esp_bt_gap_get_bond_device_num() > 0) {
        open_bonded();
    } else {
        begin_scan_round();
    }
}

/* ---- esp_timer callbacks (run in the esp_timer task) ---- */

static void lock_tick_cb(void *arg) {
    lock();
    if (g_state != ST_SCANNING) {
        unlock();
        return;
    }
    int64_t t = now_ms();

    if (g_candidate.active) {
        ewma_entry_t *e = ewma_find(g_candidate.bda);
        if (e && (t - e->last_seen_ms) > CANDIDATE_GONE_MS) {
            printf("[hid] candidate gone\n");
            memset(&g_candidate, 0, sizeof(g_candidate));
        }
    }

    if (g_candidate.active) {
        int64_t stable = t - g_candidate.set_at_ms;
        int64_t total = t - g_scan_start_ms;
        if (stable >= LOCK_WAIT_MS || total >= MAX_WAIT_MS) {
            open_candidate();
        }
    }
    unlock();
}

static void conn_timeout_cb(void *arg) {
    lock();
    if (g_state != ST_CONNECTING) {
        unlock();
        return; /* OPEN/CLOSE already resolved this attempt */
    }
    printf("[hid] connect timeout, rescan\n");
    esp_bd_addr_t tried;
    memcpy(tried, g_candidate.bda, sizeof(tried));
    memset(&g_candidate, 0, sizeof(g_candidate));
    ewma_clear();
    g_state = ST_SCANNING;
    int64_t delay = note_connect_fail(tried);
    arm_rescan(delay);
    unlock();
}

static void rescan_backoff_cb(void *arg) {
    lock();
    if (g_state == ST_CONNECTED) {
        unlock();
        return; /* raced with a successful open; stay connected */
    }
    resume_scan();
    unlock();
}

/* ---- GAP callback (runs in the BT task) ---- */

static void handle_disc_result(esp_bt_gap_cb_param_t *p) {
    const uint8_t *bda = p->disc_res.bda;
    char nm[64] = {0};
    const char *name = NULL;
    int8_t rssi = 0;

    for (int i = 0; i < p->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t *prop = &p->disc_res.prop[i];
        if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
            name = (const char *)prop->val;
        } else if (prop->type == ESP_BT_GAP_DEV_PROP_RSSI) {
            rssi = *((int8_t *)prop->val);
        } else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR) {
            uint8_t len = 0;
            uint8_t *d = esp_bt_gap_resolve_eir_data(
                (uint8_t *)prop->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len
            );
            if (!d) {
                d = esp_bt_gap_resolve_eir_data(
                    (uint8_t *)prop->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len
                );
            }
            if (d && len && !name) {
                if (len > sizeof(nm) - 1) {
                    len = sizeof(nm) - 1;
                }
                memcpy(nm, d, len);
                nm[len] = 0;
                name = nm;
            }
        }
    }

    if (!is_gamepad(name)) {
        return;
    }
    lock();
    if (g_state == ST_SCANNING) {
        int64_t t = now_ms();
        float s = ewma_update(bda, rssi, t);
        if (s != -127.0f) {
            candidate_update(bda, s, t);
        }
    }
    unlock();
}

static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
        handle_disc_result(param);
        break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        /* Continuous inquiry: Bluedroid stops inquiry after INQ_LENGTH;
         * restart immediately while still scanning (keeps candidate/EWMA). */
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            lock();
            if (g_state == ST_SCANNING) {
                esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, INQ_LENGTH, 0);
            }
            unlock();
        }
        break;
    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        /* Headless host: no keyboard. Accept with passkey 0 so SSP never stalls. */
        esp_bt_gap_ssp_passkey_reply(param->key_req.bda, true, 0);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        printf("[gap] KEY_NOTIF passkey=%06" PRIu32 "\n", param->key_notif.passkey);
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = {0};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin);
        break;
    }
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        printf(
            "[gap] AUTH_CMPL addr=%02x:%02x:%02x:%02x:%02x:%02x name=%s stat=%d %s lk=%d\n",
            param->auth_cmpl.bda[0],
            param->auth_cmpl.bda[1],
            param->auth_cmpl.bda[2],
            param->auth_cmpl.bda[3],
            param->auth_cmpl.bda[4],
            param->auth_cmpl.bda[5],
            (const char *)param->auth_cmpl.device_name,
            (int)param->auth_cmpl.stat,
            (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) ? "OK" : "FAIL",
            (int)param->auth_cmpl.lk_type
        );
        break;
    default:
        break;
    }
}

/* ---- HID callback (runs in the esp_hidh event task) ---- */

static void
hidh_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;
    const uint8_t *bda = NULL;

    switch (event_id) {
    case ESP_HIDH_OPEN_EVENT:
        bda = esp_hidh_dev_bda_get(param->open.dev);
        esp_timer_stop(g_conn_timeout);
        if (param->open.status == ESP_OK) {
            lock();
            halt_scanning_side_effects();
            g_state = ST_CONNECTED;
            memset(&g_candidate, 0, sizeof(g_candidate));
            note_connect_ok(bda);
            unlock();
            printf("[hid] open: addr=");
            print_bda(bda);
            printf(" transport=BR_EDR\n");
            hid_dump_report_map(param->open.dev);
        } else {
            printf("[hid] open FAIL: addr=");
            print_bda(bda);
            printf(" transport=BR_EDR status=0x%x\n", (unsigned)param->open.status);
            if (param->open.dev) {
                esp_hidh_dev_free(param->open.dev);
            }
            lock();
            remove_bond_of(bda); /* peer rejected our key -> drop it, next round pairs fresh */
            int64_t delay = note_connect_fail(bda);
            memset(&g_candidate, 0, sizeof(g_candidate));
            ewma_clear();
            g_state = ST_SCANNING;
            arm_rescan(delay);
            unlock();
        }
        break;
    case ESP_HIDH_INPUT_EVENT: {
        apex5_xinput_t cur;
        if (!hid_decode(param->input.data, param->input.length, &cur)) {
            break;
        }
#if HID_DEBUG_DELTA
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
        printf("[hid] close: addr=");
        print_bda(bda);
        printf(" transport=BR_EDR status=0x%x\n", (unsigned)param->close.status);
        if (param->close.dev) {
            esp_hidh_dev_free(param->close.dev);
        }
        lock();
        g_state = ST_SCANNING; /* bond kept; resume_scan() will page it */
        arm_rescan(RETRY_BACKOFF_MS);
        unlock();
        break;
    default:
        break;
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(bt_stack_start());

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(bt_gap_cb));

    esp_hidh_config_t hidh_cfg = {
        .callback = hidh_event_handler,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(esp_hidh_init(&hidh_cfg));

    g_app_mutex = xSemaphoreCreateMutex();

    const esp_timer_create_args_t tick_args = {
        .callback = lock_tick_cb,
        .name = "lock_tick",
    };
    const esp_timer_create_args_t ct_args = {.callback = conn_timeout_cb, .name = "conn_timeout"};
    const esp_timer_create_args_t rb_args = {
        .callback = rescan_backoff_cb,
        .name = "rescan_backoff",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &g_lock_tick));
    ESP_ERROR_CHECK(esp_timer_create(&ct_args, &g_conn_timeout));
    ESP_ERROR_CHECK(esp_timer_create(&rb_args, &g_rescan_backoff));

    printf("[hid] boot bonds=%d\n", esp_bt_gap_get_bond_device_num());

    lock();
    resume_scan();
    unlock();
    /* No loop: GAP/HID callbacks + esp_timer drive everything from here. */
}
