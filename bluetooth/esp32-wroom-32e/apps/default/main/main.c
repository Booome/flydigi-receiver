/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * default: BT HID host, scenarios 1 + 2.
 *
 * Scope of this milestone:
 *   - Bring up BT (bt_stack.c) + esp_hidh host.
 *   - Scenario 1: continuous inquiry; every name-matched DISC_RES opens
 *     immediately as a fresh-pair attempt. An inquiry response means the
 *     peer is in inquiry-scan mode, which on a Flydigi pad only happens
 *     while it's advertising itself for pairing — so we never use a stale
 *     NVS key on an outbound page (PAGE_TIMEOUT 0x04, pad UI stuck on
 *     "connecting"). EWMA + hysteresis still smooth multi-pad RSSI noise.
 *   - Scenario 2 (bonded pad power-cycle): purely passive — the stack
 *     handles device-initiated reconnection end to end (page -> SDP ->
 *     OPEN_EVENT is_orig=false); we only log the inbound ACL and never
 *     dev_open over it (app-initiated open races the stack's SDP:
 *     esp-idf#10504). One slow outbound probe every SLOW_PROBE_MS rescues
 *     a pad that exhausted its page budget.
 *   - On OPEN, dump descriptor + print raw INPUT reports; on FAIL/CLOSE, reset
 *     state and resume inquiry.
 *
 * Out of scope (deferred):
 *   - Asymmetric-bond recovery (pad cleared its key while NVS still has one,
 *     or NVS-only stale bond). HID report field decoding. Both follow-ups.
 *
 * State machine (event-driven, no while(1) loop):
 *   ST_SCANNING   — inquiry running; lock_tick handles the SLOW_PROBE rescue
 *                   path (no candidate lock window needed).
 *   ST_CONNECTING — dev_open issued; waiting for OPEN_EVENT or connect_timeout.
 *   ST_CONNECTED  — paired, receiving INPUT reports.
 *
 * Candidate evaluation:
 *   Layer 1 name allowlist: "Xbox Wireless Controller" / "Pro Controller".
 *   Layer 2 EWMA alpha=0.3 — smooth RSSI jitter for multi-pad stability.
 *   Open policy: every name-matched inquiry hit is treated as fresh intent —
 *     the pad is in pairing mode by definition (only then does it run
 *     inquiry scan), so any stale NVS key is dropped on the floor instead of
 *     being used for an outbound page.
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

#define HYSTERESIS_DB 3
#define LOCK_WAIT_MS 3000
#define MAX_WAIT_MS 8000
/* Bluedroid's device-initiated reconnect answers SDP late (5-35 s is
 * normal; see esp-idf#10504 and the RAPOO keyboard log on esp32.com), so
 * give an in-flight CONNECTING attempt this long before declaring it dead. */
#define CONNECT_TIMEOUT_MS 35000
#define RETRY_BACKOFF_MS 300
#define PROBE_DELAY_MS 200
#define REMOVE_BOND_POLL_MS 100
#define REMOVE_BOND_MAX_ATTEMPTS 10
#define EWMA_ALPHA_NUM 3
#define EWMA_ALPHA_DEN 10
#define MAX_BONDED_DEVICES 8
/* Rescue probe for a pad stuck in "connecting" with its page budget
 * exhausted (won't page again by itself). Deliberately slow: must not
 * interrupt the passive auto-reconnect path (a probe while an attempt is
 * in flight just gets a sync-NULL and is skipped anyway). */
#define SLOW_PROBE_MS 8000
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
static esp_timer_handle_t g_probe_after_fail = NULL;
static esp_timer_handle_t g_remove_bond_poll = NULL;

/* Pad may have cleared its key while we still have the NVS bond (long-
 * press re-pair). The probe target stored by OPEN FAIL, taken when the
 * one-shot timer fires (after the current HID event fully drains). */
static esp_bd_addr_t g_probe_after_fail_bda = {0};
/* Async remove_bond state — esp_bt_gap_remove_bond_device is fire-and-
 * forget (BTC message); the BTC task flushes the cache+NVS erase on its
 * own schedule. We poll bond count via this timer instead of blocking
 * the OPEN FAIL callback (which would stall other HID events). */
static esp_bd_addr_t g_remove_bond_target_bda = {0};
static int g_remove_bond_attempt = 0;

/* Rate limit for the stuck-pad rescue probe (see SLOW_PROBE_MS). */
static int64_t g_last_probe_ms = 0;
/* A fresh-pair/re-pair candidate (bda NOT in the NVS bond list) should
 * open immediately — pads that are actively in inquiry-scan mode are
 * expected to disappear fast if the host stalls. Bonded reconnection
 * (bda IS in the bond list) is handled by the auto path on ACL_CONN
 * and skips this flag entirely. */
static bool g_candidate_fresh = false;

/* Known Apex5 BR/EDR advertised names. Extend as new names appear. */
static const char *const g_gamepad_names[] = {
    "Xbox Wireless Controller", /* PC>BT / Android / iOS = X-input */
    "Pro Controller",           /* Nintendo Switch (NS) mode */
};

/* Forward decls for mutually recursive helpers (begin_scan_round <-> arm_rescan). */
static void begin_scan_round(void);
static void arm_rescan(int64_t delay_ms);
/* Forward decls for inbound-ACL handling (scenario 2). */
static void hidh_event_handler(
    void *arg, esp_event_base_t base, int32_t event_id, void *event_data
);
static bool is_known_bonded_bda(const uint8_t *bda);
static void init_hidh_host(void);
static void start_connect(const uint8_t *bda, const char *src, bool wipe_bond);

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

/* NVS bond list into dev_list; returns count, 0 if none or query fails. */
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

/* True if bda is in the NVS bond list (filter accidental pages). */
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

/* First NVS-bonded peer, for the outbound probe. False if none. */
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

static void set_candidate(const uint8_t *bda, float smoothed, int64_t t, const char *action) {
    g_candidate.active = true;
    memcpy(g_candidate.bda, bda, 6);
    g_candidate.smoothed = smoothed;
    g_candidate.set_at_ms = t;
    /* BT spec: an inquiry response means the peer is in inquiry-scan mode,
     * which on a Flydigi pad only happens while it's advertising itself for
     * pairing. Treat every inquiry hit as a fresh-pair intent — even if NVS
     * still has the old key (pad may have cleared its half on long-press),
     * using the stale key on an outbound page returns PAGE_TIMEOUT 0x04 and
     * the pad UI stays stuck on the connecting screen. */
    g_candidate_fresh = true;
    printf("[hid] candidate%s: addr=", action);
    print_bda(bda);
    printf(" smoothed=%.1f fresh\n", smoothed);
}

static void candidate_update(const uint8_t *bda, float smoothed, int64_t t) {
    if (!g_candidate.active) {
        set_candidate(bda, smoothed, t, "");
    } else if (bda_eq(bda, g_candidate.bda)) {
        /* same candidate; lock clock keeps running */
    } else if (smoothed >= g_candidate.smoothed + HYSTERESIS_DB) {
        set_candidate(bda, smoothed, t, " replace");
    }
}

/* lock_tick may already be stopped/not armed; INVALID_STATE is benign. */
static void stop_lock_tick(void) {
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

/* GAP callback is registered separately and survives hidh deinit/init. */
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

/* Issue a BR/EDR HID connect. Caller must hold g_app_mutex. src is a log
 * tag. NULL return from dev_open = stale wrapper TAILQ entry (no event
 * will ever come); treat as synchronous failure instead of burning a
 * full conn_timeout window.
 *
 * wipe_bond (v1 fix 241277d, ported): a peer sighted in inquiry has
 * forgotten our key (pairing mode). Drop the bond HERE, while no ACL is
 * up — bta_dm_remove_device defers the delete while the ACL is live, so
 * the FAIL-path remove lands too late: the next page then authenticates
 * with the stale key, gets 0x05 rejected, and wedges the pad UI on
 * "connecting" permanently (pad stops scanning after rejecting). The
 * BTC/BTA message queues are FIFO, so this remove executes before
 * dev_open's link-layer auth; auth then runs keyless and the pad drives
 * fresh SSP. */
static void start_connect(const uint8_t *bda, const char *src, bool wipe_bond) {
    if (g_state == ST_CONNECTING) {
        printf("[hid] start_connect: already connecting, ignore\n");
        return;
    }
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
        /* Fresh pair / re-pair: pad is in inquiry-scan, it disappears
         * if the host stalls. Open immediately on first valid hit. */
        if (g_candidate_fresh) {
            g_candidate_fresh = false;
            printf("[hid] candidate fresh: open immediately\n");
            open_candidate();
        } else {
            int64_t stable = t - g_candidate.set_at_ms;
            int64_t total = t - g_scan_start_ms;
            if (stable >= LOCK_WAIT_MS || total >= MAX_WAIT_MS) {
                open_candidate();
            }
        }
    } else if (t - g_scan_start_ms >= SLOW_PROBE_MS && t - g_last_probe_ms >= SLOW_PROBE_MS) {
        /* Long idle with a bond on file: pad likely exhausted its page
         * budget and sits stuck 'connecting'; poke it outbound. */
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

/* Async remove_bond state machine — esp_bt_gap_remove_bond_device is
 * fire-and-forget (BTC message); the BTC task flushes its cache + NVS on
 * its own schedule. After multiple re-pair cycles BTC's cache gets sticky
 * and remove_bond returns ESP_OK without actually clearing. We poll bond
 * count here instead of blocking the OPEN FAIL callback. Each step is a
 * 100 ms timer tick; total budget REMOVE_BOND_MAX_ATTEMPTS * 100 ms. */
static void remove_bond_poll_cb(void *arg) {
    lock();
    if (g_state != ST_SCANNING) {
        printf("[hid] remove_bond poll: state=%d, abort\n", (int)g_state);
        g_remove_bond_attempt = 0;
        unlock();
        return;
    }
    int nb = esp_bt_gap_get_bond_device_num();
    if (nb == 0 || g_remove_bond_attempt >= REMOVE_BOND_MAX_ATTEMPTS) {
        printf(
            "[hid] remove_bond done: attempts=%d bonds=%d%s\n",
            g_remove_bond_attempt,
            nb,
            nb ? " (GIVEUP)" : ""
        );
        g_remove_bond_attempt = 0;
        ESP_ERROR_CHECK(esp_timer_start_once(g_probe_after_fail, (uint64_t)PROBE_DELAY_MS * 1000));
        unlock();
        return;
    }
    esp_err_t err = esp_bt_gap_remove_bond_device(g_remove_bond_target_bda);
    printf("[hid] remove_bond[%d] err=0x%x\n", g_remove_bond_attempt, (unsigned)err);
    g_remove_bond_attempt++;
    ESP_ERROR_CHECK(esp_timer_start_once(g_remove_bond_poll, (uint64_t)REMOVE_BOND_POLL_MS * 1000));
    unlock();
}

/* Post-OPEN-FAIL rescue: immediately page the bonded pad (it is typically
 * stuck 'connecting' with its page budget spent, only an inbound page from
 * us restarts the handshake). Runs PROBE_DELAY_MS after the failure so the
 * HID event that triggered it fully drains first. Throttled to
 * SLOW_PROBE_MS so re-pair still gets inquiry windows. */
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
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT: {
        const uint8_t *ib = param->acl_conn_cmpl_stat.bda;
        /* Reconnect is handled entirely by the stack: the pad pages us,
         * Bluedroid accepts the ACL, does its own SDP, and raises
         * OPEN_EVENT (is_orig=false). LGC (esp-idf#10504) proves the app
         * must stay out of this — an app-level dev_open here races the
         * stack's SDP and breaks the attempt. Log and do nothing. */
        printf("[gap] ACL_CONN inbound addr=");
        print_bda(ib);
        printf(
            is_known_bonded_bda(ib) ? " (bonded, letting stack handle)\n"
                                    : " (unknown bond, ignoring)\n"
        );
        break;
    }
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT: {
        /* Clean L2CAP close: hidh CLOSE_EVT already reset state (this is
         * a no-op). Supervision-timeout close (pad power-cut): no CLOSE_EVT
         * ever fires, so reset here or g_state sticks CONNECTED and every
         * later inbound page is ignored as 'busy'. */
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
        /* conn_timeout may not be armed if OPEN came from inbound page (no
         * start_connect on that path); INVALID_STATE is benign. */
        {
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
            if (param->open.dev) {
                esp_hidh_dev_free(param->open.dev);
            }
            /* Pad may have cleared its key while we still have the NVS
             * bond (scenario 3 re-pair with long-press). Hand off to the
             * async remove_bond_poll timer; we must NOT block this HID
             * callback — blocking stalls CLOSE_EVT processing and any
             * other queued HID events. */
            lock();
            reset_scan_state();
            if (bda != NULL) {
                memcpy(g_probe_after_fail_bda, bda, sizeof(g_probe_after_fail_bda));
                memcpy(g_remove_bond_target_bda, bda, sizeof(g_remove_bond_target_bda));
                g_remove_bond_attempt = 0;
                esp_err_t start_err =
                    esp_timer_start_once(g_remove_bond_poll, (uint64_t)REMOVE_BOND_POLL_MS * 1000);
                if (start_err == ESP_ERR_INVALID_STATE) {
                    printf("[hid] remove_bond_poll already armed (benign)\n");
                } else if (start_err != ESP_OK) {
                    printf("[hid] remove_bond_poll start FAIL err=0x%x\n", (unsigned)start_err);
                }
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
    const esp_timer_create_args_t pf_args = {
        .callback = probe_after_fail_cb,
        .name = "probe_after_fail",
    };
    const esp_timer_create_args_t rbpol_args = {
        .callback = remove_bond_poll_cb,
        .name = "remove_bond_poll",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &g_lock_tick));
    ESP_ERROR_CHECK(esp_timer_create(&ct_args, &g_conn_timeout));
    ESP_ERROR_CHECK(esp_timer_create(&rb_args, &g_rescan_backoff));
    ESP_ERROR_CHECK(esp_timer_create(&pf_args, &g_probe_after_fail));
    ESP_ERROR_CHECK(esp_timer_create(&rbpol_args, &g_remove_bond_poll));

    init_hidh_host();
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(bt_gap_cb));

    printf("[hid] boot bonds=%d\n", esp_bt_gap_get_bond_device_num());

    begin_scan_round();
    /* No loop; GAP/HID callbacks + esp_timer drive everything from here. */
}
