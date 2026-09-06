/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * default: BT HID host, scenario 1 — fresh pair only.
 *
 * Scope of this milestone:
 *   - Bring up BT (bt_stack.c) + esp_hidh host.
 *   - Run continuous inquiry; on gamepad-name match, evaluate the 3-layer
 *     candidate (EWMA + hysteresis + stable/total window) and open the first
 *     matching HID device.
 *   - On OPEN, dump descriptor + print raw INPUT reports; on FAIL/CLOSE, reset
 *     state and resume inquiry.
 *
 * Out of scope (deferred):
 *   - Bonded reconnect (handles inbound ACL_CONN from a bonded pad). Scenario 2.
 *   - HID report field decoding. Deferred until raw bytes have been mapped to
 *     button/stick/trigger semantics on hardware.
 *   - Deadlock recovery, asymmetric-bond handling, g_probe, outbound page, etc.
 *     These were symptoms of older designs and are not carried into v2.
 *
 * State machine (event-driven, no while(1) loop):
 *   ST_SCANNING   — inquiry running; lock_tick evaluates candidate lock condition
 *                   every LOCK_TICK_MS. On ACL/discovery complete, auto-resume.
 *   ST_CONNECTING — dev_open issued; waiting for OPEN_EVENT or connect_timeout.
 *   ST_CONNECTED  — paired, receiving INPUT reports.
 *
 * The 3-layer candidate algorithm (constants unchanged from prior design):
 *   Layer 1 name allowlist: "Xbox Wireless Controller" / "Pro Controller".
 *   Layer 2 EWMA alpha=0.3 — smooth RSSI jitter.
 *   Layer 3 hysteresis 3 dB + lock when stable>=3s OR total>=8s.
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
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

/* Tunables (3-layer candidate algorithm). */
#define HYSTERESIS_DB 3
#define LOCK_WAIT_MS 3000
#define MAX_WAIT_MS 8000
#define CONNECT_TIMEOUT_MS 4000
#define RETRY_BACKOFF_MS 300
#define EWMA_ALPHA_NUM 3
#define EWMA_ALPHA_DEN 10
#define EWMA_MAX 16
#define LOCK_TICK_MS 250

/* Inquiry window (units of 1.28s); 8 ~= 10s per cycle. */
#define INQ_LENGTH 8

typedef struct {
    uint8_t bda[6];
    bool used;
    float smoothed;
} ewma_entry_t;

typedef struct {
    bool active;
    uint8_t bda[6];
    float smoothed;
    int64_t set_at_ms;
} candidate_t;

typedef enum { ST_SCANNING, ST_CONNECTING, ST_CONNECTED } app_state_t;

static ewma_entry_t g_ewma[EWMA_MAX];
static candidate_t g_candidate = {0};
static int64_t g_scan_start_ms = 0;
static volatile app_state_t g_state = ST_SCANNING;
static SemaphoreHandle_t g_app_mutex = NULL;

static esp_timer_handle_t g_lock_tick = NULL;
static esp_timer_handle_t g_conn_timeout = NULL;
static esp_timer_handle_t g_rescan_backoff = NULL;

/* Known Apex5 BR/EDR advertised names. Extend as new names appear. */
static const char *const g_gamepad_names[] = {
    "Xbox Wireless Controller", /* PC>BT / Android / iOS = X-input */
    "Pro Controller",           /* Nintendo Switch (NS) mode */
};

/* Forward decls for mutually recursive helpers (begin_scan_round <-> arm_rescan). */
static void begin_scan_round(void);
static void arm_rescan(int64_t delay_ms);

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

static bool bda_eq(const uint8_t *a, const uint8_t *b) {
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

static float ewma_update(const uint8_t *bda, int8_t rssi) {
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
    return e->smoothed;
}

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

static void halt_scanning_side_effects(void) {
    esp_err_t err = esp_bt_gap_cancel_discovery();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        printf("[hid] cancel_discovery err=0x%x\n", (unsigned)err);
    }
    /* Lock_tick should be armed here (we came from begin_scan_round), but if
     * it's not (e.g. failed-start path), INVALID_STATE is benign. */
    esp_err_t stop_err = esp_timer_stop(g_lock_tick);
    if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
        printf("[hid] lock_tick stop FAIL err=0x%x\n", (unsigned)stop_err);
    }
}

static void reset_scan_state(void) {
    memset(&g_candidate, 0, sizeof(g_candidate));
    ewma_clear();
    g_scan_start_ms = now_ms();
    g_state = ST_SCANNING;
}

static void issue_connect(const uint8_t *bda) {
    esp_bd_addr_t peer;
    memcpy(peer, bda, sizeof(peer));
    halt_scanning_side_effects();
    ESP_ERROR_CHECK(esp_timer_start_once(g_conn_timeout, (uint64_t)CONNECT_TIMEOUT_MS * 1000));
    g_state = ST_CONNECTING;
    esp_hidh_dev_open(peer, ESP_HID_TRANSPORT_BT, 0);
}

static void open_candidate(void) {
    printf("[hid] connecting: addr=");
    print_bda(g_candidate.bda);
    printf(" transport=BR_EDR\n");
    issue_connect(g_candidate.bda);
}

static void begin_scan_round(void) {
    reset_scan_state();
    /* Idempotent stop: lock_tick may already be armed (e.g. on DISC_STATE
     * auto-restart after INQ_LENGTH). INVALID_STATE here means "not armed",
     * which is benign. ESP_ERROR_CHECK would crash on that benign case. */
    esp_err_t stop_err = esp_timer_stop(g_lock_tick);
    if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
        printf("[hid] lock_tick stop FAIL err=0x%x\n", (unsigned)stop_err);
    }
    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, INQ_LENGTH, 0);
    if (err != ESP_OK) {
        printf("[hid] start_discovery FAIL err=0x%x, arm rescan\n", (unsigned)err);
        arm_rescan(RETRY_BACKOFF_MS);
        return;
    }
    err = esp_timer_start_periodic(g_lock_tick, (uint64_t)LOCK_TICK_MS * 1000);
    if (err != ESP_OK) {
        printf(
            "[hid] lock_tick start FAIL err=0x%x, cancel discovery, arm rescan\n", (unsigned)err
        );
        esp_bt_gap_cancel_discovery();
        arm_rescan(RETRY_BACKOFF_MS);
        return;
    }
}

static void arm_rescan(int64_t delay_ms) {
    esp_err_t err = esp_timer_start_once(g_rescan_backoff, (uint64_t)delay_ms * 1000);
    if (err == ESP_ERR_INVALID_STATE) {
        return;
    }
    if (err != ESP_OK) {
        printf("[hid] rescan_backoff start FAIL err=0x%x, immediate rescan\n", (unsigned)err);
        begin_scan_round();
    }
}

/* esp_timer callbacks run in the esp_timer task. */

static void lock_tick_cb(void *arg) {
    lock();
    if (g_state != ST_SCANNING) {
        unlock();
        return;
    }
    int64_t t = now_ms();
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
        return;
    }
    printf("[hid] connect timeout, rescan\n");
    reset_scan_state();
    arm_rescan(RETRY_BACKOFF_MS);
    unlock();
}

static void rescan_backoff_cb(void *arg) {
    lock();
    if (g_state == ST_CONNECTED) {
        unlock();
        return;
    }
    begin_scan_round();
    unlock();
}

/* GAP callback (BT task). */

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
        float s = ewma_update(bda, rssi);
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
        /* Continuous inquiry: Bluedroid stops inquiry after INQ_LENGTH; restart
         * immediately. Only inquiry needs restarting — lock_tick is periodic
         * and already armed from begin_scan_round(), so re-arming would fail
         * with INVALID_STATE. Keep this path minimal. */
        printf("[gap] DISC_STATE state=%d\n", (int)param->disc_st_chg.state);
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            lock();
            bool should_restart = (g_state == ST_SCANNING);
            unlock();
            if (should_restart) {
                esp_err_t err =
                    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, INQ_LENGTH, 0);
                if (err != ESP_OK) {
                    printf("[hid] restart discovery FAIL err=0x%x, arm rescan\n", (unsigned)err);
                    arm_rescan(RETRY_BACKOFF_MS);
                }
            }
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
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        /* Scenario 1 does not handle inbound bonded reconnects. Defer to scenario 2. */
        {
            const uint8_t *ib = param->acl_conn_cmpl_stat.bda;
            printf("[gap] ACL_CONN inbound addr=");
            print_bda(ib);
            printf(" (scenario 1: not handled; defer to scenario 2)\n");
        }
        break;
    default:
        break;
    }
}

/* HID callback (esp_hidh event task). */

static void
hidh_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;
    const uint8_t *bda = NULL;

    switch (event_id) {
    case ESP_HIDH_OPEN_EVENT:
        bda = esp_hidh_dev_bda_get(param->open.dev);
        /* conn_timeout may not be armed if OPEN came from inbound page (no
         * issue_connect on that path); INVALID_STATE is benign. */
        {
            esp_err_t stop_err = esp_timer_stop(g_conn_timeout);
            if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
                printf("[hid] conn_timeout stop FAIL err=0x%x\n", (unsigned)stop_err);
            }
        }
        if (param->open.status == ESP_OK) {
            lock();
            halt_scanning_side_effects();
            g_state = ST_CONNECTED;
            memset(&g_candidate, 0, sizeof(g_candidate));
            unlock();
            printf("[hid] open: addr=");
            print_bda(bda);
            printf(" transport=BR_EDR\n");
            hid_report_dump_map(param->open.dev);
        } else {
            printf("[hid] open FAIL: addr=");
            print_bda(bda);
            printf(" transport=BR_EDR status=0x%x\n", (unsigned)param->open.status);
            if (param->open.dev) {
                esp_hidh_dev_free(param->open.dev);
            }
            lock();
            reset_scan_state();
            arm_rescan(RETRY_BACKOFF_MS);
            unlock();
        }
        break;
    case ESP_HIDH_INPUT_EVENT: {
        uint8_t rid = 0;
        if (param->input.length > 0) {
            rid = param->input.data[0];
        }
        hid_report_on_input(param->input.data, param->input.length, rid);
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
        reset_scan_state();
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

    g_app_mutex = xSemaphoreCreateMutex();

    const esp_timer_create_args_t tick_args = {
        .callback = lock_tick_cb,
        .name = "lock_tick",
    };
    const esp_timer_create_args_t ct_args = {
        .callback = conn_timeout_cb,
        .name = "conn_timeout",
    };
    const esp_timer_create_args_t rb_args = {
        .callback = rescan_backoff_cb,
        .name = "rescan_backoff",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &g_lock_tick));
    ESP_ERROR_CHECK(esp_timer_create(&ct_args, &g_conn_timeout));
    ESP_ERROR_CHECK(esp_timer_create(&rb_args, &g_rescan_backoff));

    esp_hidh_config_t hidh_cfg = {
        .callback = hidh_event_handler,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(esp_hidh_init(&hidh_cfg));
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(bt_gap_cb));

    printf("[hid] boot bonds=%d\n", esp_bt_gap_get_bond_device_num());

    begin_scan_round();
    /* No loop; GAP/HID callbacks + esp_timer drive everything from here. */
}
