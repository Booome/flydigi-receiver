#include "soc_osal.h"
#include "errcode.h"
#include "sle_common.h"
#include "sle_device_manager.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"

#include "led.h"
#include "conn_nv.h"
#include "button.h"
#include "scan_table.h"
#include "bs21_util.h"
#include "conn_mgr.h"

#define RED_PIN   S_MGPIO11
#define BLUE_PIN  S_MGPIO13

#define SCAN_PRINT_MS  2000
#define TICK_MS        10

static sle_dev_manager_callbacks_t g_dev_cbk = { 0 };
static sle_announce_seek_callbacks_t g_seek_cbk = { 0 };
static sle_connection_callbacks_t g_conn_cbk = { 0 };

static uint32_t g_now_ms = 0;

static void create_task(osal_kthread_handler handler, const char *name)
{
    osal_task *task_handle = osal_kthread_create(handler, 0, name, 0x1000);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, 24);
        osal_kfree(task_handle);
    }
}

static void *scan_task(const char *arg)
{
    (void)arg;
    while (1) {
        osal_msleep(SCAN_PRINT_MS);
        if (conn_mgr_is_scanning()) {
            scan_table_print();
        }
    }
    return NULL;
}

static void *tick_task(const char *arg)
{
    (void)arg;
    while (1) {
        osal_msleep(TICK_MS);
        g_now_ms += TICK_MS;
        conn_mgr_tick(g_now_ms);
    }
    return NULL;
}

static void sle_power_on_cb(uint8_t status)
{
    osal_printk("sle power on: %d\r\n", status);
    enable_sle();
}

static void sle_enable_cb(uint8_t status)
{
    osal_printk("sle enable: %d\r\n", status);
    if (status == 0) {
        sle_addr_t la = { 0 };
        if (sle_get_local_addr(&la) == ERRCODE_SUCC) {
            osal_printk("[conn] local addr: %02x:%02x:%02x:%02x:%02x:%02x type:%d\r\n",
                        la.addr[0], la.addr[1], la.addr[2],
                        la.addr[3], la.addr[4], la.addr[5], la.type);
        }
        sle_setup_set_local_addr();
        conn_mgr_start();
    }
}

static void seek_enable_cb(errcode_t status)
{
    conn_mgr_seek_enable(status);
}

static void seek_result_cb(sle_seek_result_info_t *result)
{
    conn_mgr_seek_result(result);
}

static void seek_disable_cb(errcode_t status)
{
    conn_mgr_seek_disable(status);
}

static void conn_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
                                  sle_acb_state_t conn_state, sle_pair_state_t pair_state,
                                  sle_disc_reason_t disc_reason)
{
    conn_mgr_state_changed(conn_id, addr, conn_state, pair_state, disc_reason);
}

static void pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    conn_mgr_pair_complete(conn_id, addr, status);
}

static void conn_param_update_cb(uint16_t conn_id, errcode_t status,
                                 const sle_connection_param_update_evt_t *param)
{
    conn_mgr_param_update(conn_id, status, param);
}

static void auth_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status,
                             const sle_auth_info_evt_t *evt)
{
    conn_mgr_auth_complete(conn_id, addr, status, evt);
}

void axk_main(void)
{
    osal_printk("app: flydigi-wireless\r\n");
    bs21_rst();

    led_t led_red = led_init(RED_PIN);
    led_t led_blue = led_init(BLUE_PIN);
    button_init();
    conn_nv_init();

    conn_mgr_init(led_red, led_blue);

    button_set_cb(conn_mgr_on_long_press, conn_mgr_on_short_press,
                  conn_mgr_on_very_long_press);

    g_dev_cbk.sle_power_on_cb = sle_power_on_cb;
    g_dev_cbk.sle_enable_cb = sle_enable_cb;
    sle_dev_manager_register_callbacks(&g_dev_cbk);

    g_seek_cbk.seek_enable_cb = seek_enable_cb;
    g_seek_cbk.seek_result_cb = seek_result_cb;
    g_seek_cbk.seek_disable_cb = seek_disable_cb;
    sle_announce_seek_register_callbacks(&g_seek_cbk);

    g_conn_cbk.connect_state_changed_cb = conn_state_changed_cb;
    g_conn_cbk.pair_complete_cb = pair_complete_cb;
    g_conn_cbk.connect_param_update_cb = conn_param_update_cb;
    g_conn_cbk.auth_complete_cb = auth_complete_cb;
    sle_connection_register_callbacks(&g_conn_cbk);

    osal_kthread_lock();
    create_task((osal_kthread_handler)scan_task, "scan_task");
    create_task((osal_kthread_handler)tick_task, "tick_task");
    create_task((osal_kthread_handler)button_task, "button_task");
    osal_kthread_unlock();

    enable_sle();
}
