#include "conn_mgr.h"
#include "bs21_util.h"
#include "cmsis_os2.h"
#include "conn_nv.h"
#include "rssi_pick.h"
#include "scan_table.h"
#include "securec.h"
#include "soc_osal.h"
#include "string.h"

#define PAIR_TIMEOUT_MS 120000
#define PAIR_SUPERV_MS 200
#define SEARCH_BLINK_MS 125
#define RECONNECT_BLINK_MS 1000

#define MS2TICK(ms) ((uint32_t)(((uint64_t)(ms) * osKernelGetTickFreq()) / 1000))

typedef enum {
    CONN_STATE_FATAL = 0,
    CONN_STATE_RECONNECT,
    CONN_STATE_SEARCH,
    CONN_STATE_ACTIVE,
} conn_state_t;

static conn_state_t g_conn_state = CONN_STATE_SEARCH;
static bool g_search_timeout = false;
static bool g_target_locked = false;
static bool g_seek_active = false;
static sle_addr_t g_target_addr = {0};

static uint8_t g_record_addr[SLE_ADDR_LEN] = {0};
static bool g_record_valid = false;
static sle_addr_t g_peer_addr = {0};

static led_t g_led_red;
static led_t g_led_blue;
static button_t g_btn;

static osTimerId_t g_pair_timer = NULL;

static void conn_apply_state_led(void);

static void conn_enter_fatal(void) {
    g_conn_state = CONN_STATE_FATAL;
    led_off(g_led_blue);
    led_on(g_led_red);
    osal_printk("[conn] FATAL: NV broken, blocking connection flow\r\n");
    osKernelLock();
}

static void conn_start_search(bool timeout) {
    g_conn_state = CONN_STATE_SEARCH;
    g_search_timeout = timeout;
    if (timeout) {
        osTimerStart(g_pair_timer, MS2TICK(PAIR_TIMEOUT_MS));
    } else {
        osTimerStop(g_pair_timer);
    }
    g_target_locked = false;
    rssi_pick_init();
    osal_printk("[conn] search%s (scan)\r\n", timeout ? " +pair" : "");
    if (!g_seek_active) {
        sle_scan_start();
    }
    conn_apply_state_led();
}

static void conn_start_reconnect(void) {
    g_conn_state = CONN_STATE_RECONNECT;
    g_search_timeout = false;
    osTimerStop(g_pair_timer);
    g_target_locked = false;
    osal_printk("[conn] reconnect to record\r\n");
    if (!g_seek_active) {
        sle_scan_start();
    }
    conn_apply_state_led();
}

static void conn_rescan(void) {
    if (g_record_valid) {
        conn_start_reconnect();
    } else {
        conn_start_search(false);
    }
}

static void conn_exit_search_timeout(void) {
    if (!g_search_timeout) {
        return;
    }
    osal_printk("[conn] search timeout exit\r\n");
    g_search_timeout = false;
    osTimerStop(g_pair_timer);
    if (g_conn_state == CONN_STATE_ACTIVE) {
        osal_printk("[conn] disconnecting current link\r\n");
        sle_disconnect_remote_device(&g_peer_addr);
    }
    conn_rescan();
}

static void lock_and_connect(const sle_addr_t *addr) {
    memcpy_s(&g_target_addr, sizeof(g_target_addr), addr, sizeof(g_target_addr));
    g_target_locked = true;
    osal_printk("[conn] target locked, stopping seek\r\n");
    sle_stop_seek();
}

static void conn_mgr_on_long_press(void) {
    osal_printk("[btn] long press\r\n");
    if (g_conn_state == CONN_STATE_ACTIVE) {
        osal_printk("[conn] disconnecting current link\r\n");
        sle_disconnect_remote_device(&g_peer_addr);
    }
    conn_start_search(g_record_valid);
}

static void conn_mgr_on_short_press(void) {
    osal_printk("[btn] short press\r\n");
    if (g_search_timeout) {
        osTimerStop(g_pair_timer);
        conn_exit_search_timeout();
    }
}

static void conn_mgr_on_very_long_press(void) {
    osal_printk("[btn] very long press, erase record\r\n");
    if (g_conn_state == CONN_STATE_ACTIVE) {
        osal_printk("[conn] disconnecting current link\r\n");
        sle_disconnect_remote_device(&g_peer_addr);
    }
    if (!conn_nv_erase()) {
        conn_enter_fatal();
        return;
    }
    g_record_valid = false;
    memset_s(g_record_addr, sizeof(g_record_addr), 0, sizeof(g_record_addr));
    conn_start_search(false);
}

static void conn_apply_state_led(void) {
    switch (g_conn_state) {
    case CONN_STATE_SEARCH:
        led_blink(g_led_blue, SEARCH_BLINK_MS);
        break;
    case CONN_STATE_RECONNECT:
        led_blink(g_led_blue, RECONNECT_BLINK_MS);
        break;
    case CONN_STATE_ACTIVE:
        led_off(g_led_blue);
        break;
    default:
        break;
    }
}

static void pair_timeout_cb(void *arg) {
    if (g_conn_state == CONN_STATE_SEARCH && g_search_timeout) {
        osal_printk("[conn] search timeout\r\n");
        conn_exit_search_timeout();
    }
}

static void on_btn_hold(uint32_t held_ms, void *ctx) {
    if (held_ms == 3000) {
        led_blink(g_led_blue, 125);
    } else if (held_ms == 10000) {
        led_stop_blinking(g_led_blue);
        led_on(g_led_blue);
    }
}

static void on_btn_up(uint32_t held_ms, void *ctx) {
    if (held_ms < 3000) {
        conn_mgr_on_short_press();
    } else if (held_ms < 10000) {
        conn_mgr_on_long_press();
    } else {
        conn_mgr_on_very_long_press();
    }
}

void conn_mgr_seek_result(sle_seek_result_info_t *result) {
    scan_device_t *dev;
    if (result == NULL) {
        return;
    }
    dev = scan_table_find(&result->addr);
    if (dev == NULL) {
        dev = scan_table_add(&result->addr);
        if (dev == NULL) {
            return;
        }
    }
    dev->count++;
    dev->rssi = result->rssi;

    if (g_target_locked) {
        return;
    }

    if (g_conn_state == CONN_STATE_RECONNECT) {
        if (memcmp(result->addr.addr, g_record_addr, SLE_ADDR_LEN) == 0) {
            lock_and_connect(&result->addr);
        }
        return;
    }

    /* SEARCH: RSSI proximity selection */
    if (rssi_pick_feed(result->addr.addr, result->rssi)) {
        memcpy_s(g_target_addr.addr, SLE_ADDR_LEN, rssi_pick_locked_addr(), SLE_ADDR_LEN);
        g_target_addr.type = result->addr.type;
        g_target_locked = true;
        osal_printk("[conn] rssi target locked, stopping seek\r\n");
        sle_stop_seek();
    }
}

void conn_mgr_state_changed(uint16_t conn_id, const sle_addr_t *addr, sle_acb_state_t conn_state,
                            sle_pair_state_t pair_state, sle_disc_reason_t disc_reason) {
    osal_printk("[conn] conn id:%u state:%d pair:%d disc:0x%x\r\n", conn_id, conn_state, pair_state,
                disc_reason);
    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        if (addr != NULL) {
            memcpy_s(&g_peer_addr, sizeof(g_peer_addr), addr, sizeof(g_peer_addr));
        }
        g_conn_state = CONN_STATE_ACTIVE;
        osTimerStop(g_pair_timer);
        conn_apply_state_led();
        if (pair_state == SLE_PAIR_NONE) {
            osal_printk("[conn] pairing...\r\n");
            if (sle_pair_remote_device(addr) != ERRCODE_SUCC) {
                osal_printk("[conn] pair request fail\r\n");
            }
        }
    } else {
        osal_printk("[conn] disconnected, rescan\r\n");
        conn_rescan();
    }
}

void conn_mgr_pair_complete(uint16_t conn_id, const sle_addr_t *addr, errcode_t status) {
    osal_printk("[conn] paired: 0x%x\r\n", status);
    if (status == ERRCODE_SUCC) {
        osal_printk("[conn] pairing done, keep connection\r\n");
        if (g_search_timeout && addr != NULL &&
            memcmp(addr->addr, g_target_addr.addr, SLE_ADDR_LEN) != 0) {
            osal_printk("[conn] paired stale target, ignore\r\n");
        } else if ((g_search_timeout || !g_record_valid) && addr != NULL) {
            if (conn_nv_save(addr->addr)) {
                g_record_valid = true;
                memcpy_s(g_record_addr, SLE_ADDR_LEN, addr->addr, SLE_ADDR_LEN);
                osal_printk("[conn] record saved\r\n");
            }
            if (conn_nv_is_fatal()) {
                conn_enter_fatal();
                return;
            }
            g_search_timeout = false;
        }
        sle_connection_param_update_t up = {0};
        up.conn_id = conn_id;
        up.interval_min = 100;
        up.interval_max = 100;
        up.max_latency = 0;
        up.supervision_timeout = PAIR_SUPERV_MS;
        osal_printk("[conn] sending param update (superv=%ums)\r\n", PAIR_SUPERV_MS * 10);
        if (sle_update_connect_param(&up) != ERRCODE_SUCC) {
            osal_printk("[conn] param update send fail\r\n");
        }
        g_conn_state = CONN_STATE_ACTIVE;
    }
}

void conn_mgr_param_update(uint16_t conn_id, errcode_t status,
                           const sle_connection_param_update_evt_t *param) {
    osal_printk("[conn] param update result: id:%u status:0x%x interval:%u "
                "latency:%u superv:%u\r\n",
                conn_id, status, (param ? param->interval : 0), (param ? param->latency : 0),
                (param ? param->supervision : 0));
}

void conn_mgr_auth_complete(uint16_t conn_id, const sle_addr_t *addr, errcode_t status,
                            const sle_auth_info_evt_t *evt) {
    osal_printk("[conn] auth complete: id:%u status:0x%x\r\n", conn_id, status);
    if (status != ERRCODE_SUCC || evt == NULL) {
        return;
    }
    sle_addr_t own_addr = {0};
    if (sle_get_local_addr(&own_addr) != ERRCODE_SUCC) {
        osal_printk("[conn] get local addr fail, skip key save\r\n");
        return;
    }
    if (sle_set_nv_smp_keys((sle_auth_info_evt_t *)evt, &own_addr, (sle_addr_t *)addr, 0) !=
        ERRCODE_SUCC) {
        osal_printk("[conn] save smp keys fail\r\n");
        return;
    }
    osal_printk("[conn] smp keys saved\r\n");
}

void conn_mgr_seek_enable(errcode_t status) {
    osal_printk("seek enable: 0x%x\r\n", status);
    if (status == ERRCODE_SUCC) {
        g_seek_active = true;
    }
}

void conn_mgr_seek_disable(errcode_t status) {
    osal_printk("[conn] seek disabled: 0x%x\r\n", status);
    g_seek_active = false;
    if (g_target_locked) {
        osal_printk("[conn] connecting...\r\n");
        if (sle_connect_remote_device(&g_target_addr) != ERRCODE_SUCC) {
            osal_printk("[conn] connect fail\r\n");
            conn_rescan();
        }
        return;
    }
    if (status != ERRCODE_SUCC) {
        osal_msleep(100);
    }
    osal_printk("[conn] restart scan\r\n");
    sle_scan_start();
}

void conn_mgr_init(led_t led_red, led_t led_blue, button_t btn) {
    g_led_red = led_red;
    g_led_blue = led_blue;
    g_btn = btn;
    g_pair_timer = osTimerNew(pair_timeout_cb, osTimerOnce, NULL, NULL);
    g_record_valid = conn_nv_load(g_record_addr);
    if (conn_nv_is_fatal()) {
        conn_enter_fatal();
        return;
    }
    if (g_record_valid) {
        osal_printk("[conn] record: %02x:%02x:%02x:%02x:%02x:%02x\r\n", g_record_addr[0],
                    g_record_addr[1], g_record_addr[2], g_record_addr[3], g_record_addr[4],
                    g_record_addr[5]);
    } else {
        osal_printk("[conn] no record, search\r\n");
    }

    button_set_hold_cb(g_btn, on_btn_hold, NULL);
    button_set_up_cb(g_btn, on_btn_up, NULL);
}

void conn_mgr_start(void) {
    conn_rescan();
}
