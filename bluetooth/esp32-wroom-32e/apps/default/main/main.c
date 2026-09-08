/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * BT HID host for Flydigi Apex5: continuous inquiry pairs any name-matched
 * pad in pairing mode (wipe stale bond, fresh SSP); bonded pads reconnect
 * passively via the stack. Event-driven; design analysis in
 * docs/superpowers/specs/2026-09-07-bt-hid-scenario2-bonded-reconnect-design.md
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh.h"
#include "bt_stack.h"
#include "hid_report.h"

#define HYSTERESIS_DB 3
/* Device-initiated reconnect answers SDP late (5-35 s normal, esp-idf#10504). */
#define CONNECT_TIMEOUT_MS 35000
#define RETRY_BACKOFF_MS 300
#define PROBE_DELAY_MS 200
#define EWMA_ALPHA_NUM 3
#define EWMA_ALPHA_DEN 10
#define MAX_BONDED_DEVICES 8
/* Outbound poke for a pad that exhausted its page budget; deliberately slow
 * so it never crowds out the passive reconnect / inquiry paths. */
#define SLOW_PROBE_MS 8000
#define EWMA_MAX 16
#define LOCK_TICK_MS 250

/* Inquiry window in units of 1.28 s; 8 ~= 10 s per cycle. */
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
static esp_timer_handle_t g_probe_after_fail = NULL;

/* Probe target stored by OPEN FAIL and consumed by the one-shot timer, so
 * the page runs after the failing HID event drains. */
static esp_bd_addr_t g_probe_after_fail_bda = {0};

static int64_t g_last_probe_ms = 0;

/* Pad advertises a different name per connection mode (see docs/controller-modes.md). */
static const char *const g_gamepad_names[] = {
    "Xbox Wireless Controller", /* PC>BT / Android / iOS (X-input) */
    "Pro Controller",           /* Nintendo Switch mode */
};

static void begin_scan_round(void);
static void arm_rescan(int64_t delay_ms);
static void hidh_event_handler(
    void *arg, esp_event_base_t base, int32_t event_id, void *event_data
);

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
    if (!bda) {
        printf("unknown");
        return;
    }
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

static int bonded_list(esp_bd_addr_t *dev_list) {
    if (esp_bt_gap_get_bond_device_num() <= 0) {
        return 0;
    }
    int dev_num = MAX_BONDED_DEVICES;
    if (esp_bt_gap_get_bond_device_list(&dev_num, dev_list) != ESP_OK) {
        return 0;
    }
    return dev_num;
}

static bool is_known_bonded_bda(const uint8_t *bda) {
    esp_bd_addr_t dev_list[MAX_BONDED_DEVICES];
    int n = bonded_list(dev_list);
    for (int i = 0; i < n; i++) {
        if (bda_eq(dev_list[i], bda)) {
            return true;
        }
    }
    return false;
}

static bool first_bonded_bda(esp_bd_addr_t out) {
    esp_bd_addr_t dev_list[MAX_BONDED_DEVICES];
    if (bonded_list(dev_list) == 0) {
        return false;
    }
    memcpy(out, dev_list[0], sizeof(esp_bd_addr_t));
    return true;
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

static void set_candidate(const uint8_t *bda, float smoothed, const char *action) {
    g_candidate.active = true;
    memcpy(g_candidate.bda, bda, 6);
    g_candidate.smoothed = smoothed;
    printf("[hid] candidate%s: addr=", action);
    print_bda(bda);
    printf(" smoothed=%.1f fresh\n", smoothed);
}

static void candidate_update(const uint8_t *bda, float smoothed) {
    if (!g_candidate.active) {
        set_candidate(bda, smoothed, "");
    } else if (bda_eq(bda, g_candidate.bda)) {
        /* keep incumbent: its own EWMA drift must not restart the baseline */
    } else if (smoothed >= g_candidate.smoothed + HYSTERESIS_DB) {
        set_candidate(bda, smoothed, " replace");
    }
}

static void stop_lock_tick(void) {
    /* INVALID_STATE = not armed, the normal case at rescan entry. */
    esp_err_t err = esp_timer_stop(g_lock_tick);
    if (err == ESP_ERR_INVALID_STATE) {
        printf("[hid] lock_tick stop not armed (benign)\n");
    } else if (err != ESP_OK) {
        printf("[hid] lock_tick stop FAIL err=0x%x\n", (unsigned)err);
    }
}

static void halt_scanning_side_effects(void) {
    esp_err_t err = esp_bt_gap_cancel_discovery();
    if (err == ESP_ERR_INVALID_STATE) {
        printf("[hid] cancel_discovery not running (benign)\n");
    } else if (err != ESP_OK) {
        printf("[hid] cancel_discovery err=0x%x\n", (unsigned)err);
    }
    stop_lock_tick();
}

static void reset_scan_state(void) {
    memset(&g_candidate, 0, sizeof(g_candidate));
    ewma_clear();
    g_scan_start_ms = now_ms();
    g_state = ST_SCANNING;
}

static void init_hidh_host(void) {
    esp_hidh_config_t hidh_cfg = {
        .callback = hidh_event_handler,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    esp_err_t err = esp_hidh_init(&hidh_cfg);
    if (err != ESP_OK) {
        printf("[hid] esp_hidh_init FAIL err=0x%x\n", (unsigned)err);
    }
}

/* Caller holds g_app_mutex. wipe_bond: inquiry-visible pad has cleared its
 * key — drop ours first so dev_open authenticates keyless (spec 3.4). */
static void start_connect(const uint8_t *bda, const char *src, bool wipe_bond) {
    esp_bd_addr_t peer;
    memcpy(peer, bda, sizeof(peer));
    if (wipe_bond && is_known_bonded_bda(peer)) {
        esp_bt_gap_remove_bond_device(peer);
        printf("[hid] drop stale bond before open src=%s\n", src);
    }
    halt_scanning_side_effects();
    ESP_ERROR_CHECK(esp_timer_start_once(g_conn_timeout, (uint64_t)CONNECT_TIMEOUT_MS * 1000));
    g_state = ST_CONNECTING;
    printf("[hid] connecting: addr=");
    print_bda(peer);
    printf(" transport=BR_EDR src=%s\n", src);
    if (esp_hidh_dev_open(peer, ESP_HID_TRANSPORT_BT, 0) == NULL) {
        printf("[hid] dev_open NULL (stale TAILQ), drop to scan\n");
        ESP_ERROR_CHECK(esp_timer_stop(g_conn_timeout));
        reset_scan_state();
        arm_rescan(RETRY_BACKOFF_MS);
    }
}

static void open_candidate(void) {
    printf("[hid] candidate fresh: open immediately\n");
    start_connect(g_candidate.bda, "candidate", true);
}

static void begin_scan_round(void) {
    reset_scan_state();
    stop_lock_tick();
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
        printf("[hid] rescan_backoff already armed (benign)\n");
        return;
    }
    if (err != ESP_OK) {
        printf("[hid] rescan_backoff start FAIL err=0x%x, immediate rescan\n", (unsigned)err);
        begin_scan_round();
    }
}

static void lock_tick_cb(void *arg) {
    lock();
    if (g_state != ST_SCANNING) {
        unlock();
        return;
    }
    int64_t t = now_ms();
    if (g_candidate.active) {
        open_candidate();
    } else if (t - g_scan_start_ms >= SLOW_PROBE_MS && t - g_last_probe_ms >= SLOW_PROBE_MS) {
        esp_bd_addr_t bonded;
        if (first_bonded_bda(bonded)) {
            g_last_probe_ms = t;
            printf("[hid] probe bonded pad\n");
            start_connect(bonded, "probe", false);
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

/* A pad that just failed auth is out of page budget; only our inbound page
 * revives it. PROBE_DELAY_MS lets the failing HID event drain first. */
static void probe_after_fail_cb(void *arg) {
    lock();
    if (g_state != ST_SCANNING) {
        unlock();
        return;
    }
    int64_t t = now_ms();
    if (t - g_last_probe_ms >= SLOW_PROBE_MS) {
        g_last_probe_ms = t;
        printf("[hid] post-fail probe\n");
        start_connect(g_probe_after_fail_bda, "probe", true);
        unlock();
        return;
    }
    unlock();
    arm_rescan(RETRY_BACKOFF_MS);
}

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
        float s = ewma_update(bda, rssi);
        if (s != -127.0f) {
            candidate_update(bda, s);
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
        /* Bluedroid stops inquiry after INQ_LENGTH; only inquiry is re-armed
         * here — re-arming the periodic lock_tick caused a start-fail loop. */
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
        /* Headless: accept passkey 0 so SSP never stalls. */
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
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT: {
        /* Bonded reconnect is stack-owned (SDP -> OPEN_EVENT is_orig=false);
         * an app dev_open here races that SDP — esp-idf#10504. Log only. */
        const uint8_t *ib = param->acl_conn_cmpl_stat.bda;
        printf("[gap] ACL_CONN inbound addr=");
        print_bda(ib);
        printf(
            is_known_bonded_bda(ib) ? " (bonded, letting stack handle)\n"
                                    : " (unknown bond, ignoring)\n"
        );
        break;
    }
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT: {
        /* Supervision-timeout close fires no hidh CLOSE_EVT; without this
         * reset g_state sticks CONNECTED and inbound pages look 'busy'. */
        const uint8_t *ib = param->acl_disconn_cmpl_stat.bda;
        printf("[gap] ACL_DISCONN addr=");
        print_bda(ib);
        printf(
            " reason=0x%x handle=0x%x\n",
            (unsigned)param->acl_disconn_cmpl_stat.reason,
            (unsigned)param->acl_disconn_cmpl_stat.handle
        );
        lock();
        bool was_connected = (g_state == ST_CONNECTED);
        if (was_connected) {
            printf("[gap] ACL_DISCONN: reset ST_CONNECTED -> ST_SCANNING\n");
            g_state = ST_SCANNING;
        }
        unlock();
        if (was_connected) {
            arm_rescan(RETRY_BACKOFF_MS);
        }
        break;
    }
    default:
        break;
    }
}

static void hidh_event_handler(
    void *arg, esp_event_base_t base, int32_t event_id, void *event_data
) {
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;
    const uint8_t *bda = NULL;

    switch (event_id) {
    case ESP_HIDH_OPEN_EVENT:
        /* dev can be NULL on failure paths (esp-idf#10504). */
        bda = param->open.dev ? esp_hidh_dev_bda_get(param->open.dev) : NULL;
        {
            /* Inbound-driven OPEN never armed the timer; benign. */
            esp_err_t stop_err = esp_timer_stop(g_conn_timeout);
            if (stop_err == ESP_ERR_INVALID_STATE) {
                printf("[hid] conn_timeout stop not armed (benign)\n");
            } else if (stop_err != ESP_OK) {
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
            /* Drop our key now (deferred to link-down): the slow probe opens
             * without wiping, so a leftover bond would re-trigger 0x05. */
            lock();
            reset_scan_state();
            if (bda != NULL) {
                memcpy(g_probe_after_fail_bda, bda, sizeof(g_probe_after_fail_bda));
                esp_bd_addr_t mutable_bda;
                memcpy(mutable_bda, bda, sizeof(mutable_bda));
                esp_err_t reb_err = esp_bt_gap_remove_bond_device(mutable_bda);
                printf("[hid] remove_bond err=0x%x\n", (unsigned)reb_err);
                ESP_ERROR_CHECK(
                    esp_timer_start_once(g_probe_after_fail, (uint64_t)PROBE_DELAY_MS * 1000)
                );
            } else {
                arm_rescan(RETRY_BACKOFF_MS);
            }
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
    const esp_timer_create_args_t pf_args = {
        .callback = probe_after_fail_cb,
        .name = "probe_after_fail",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &g_lock_tick));
    ESP_ERROR_CHECK(esp_timer_create(&ct_args, &g_conn_timeout));
    ESP_ERROR_CHECK(esp_timer_create(&rb_args, &g_rescan_backoff));
    ESP_ERROR_CHECK(esp_timer_create(&pf_args, &g_probe_after_fail));

    init_hidh_host();
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(bt_gap_cb));

    printf("[hid] boot bonds=%d\n", esp_bt_gap_get_bond_device_num());

    begin_scan_round();
}
