#include "soc_osal.h"
#include "systick.h"
#include "securec.h"
#include "sle_common.h"
#include "sle_device_manager.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "scan_table.h"
#include "bs21_util.h"
#include "sle_probe_client.h"

#define PROBE_LOG        "[probe]"
#define PROBE_SCAN_MS    5000

static sle_dev_manager_callbacks_t g_dev_cbk = { 0 };
static sle_announce_seek_callbacks_t g_seek_cbk = { 0 };
static sle_connection_callbacks_t g_conn_cbk = { 0 };
static sle_addr_t g_target_addr = { 0 };
static uint16_t g_conn_id = 0;
static uint32_t g_scan_start_ms = 0;

static void probe_start_scan(void)
{
    g_scan_start_ms = uapi_systick_get_ms();
    sle_scan_start();
}

static void probe_connect_best(void)
{
    scan_device_t *best = scan_table_best();
    if (best == NULL) {
        osal_printk("%s no device found, rescan\r\n", PROBE_LOG);
        scan_table_reset();
        probe_start_scan();
        return;
    }
    memcpy_s(&g_target_addr, sizeof(sle_addr_t), &best->addr, sizeof(sle_addr_t));
    osal_printk("%s pick best: %02x:%02x:%02x:%02x:%02x:%02x rssi=%d\r\n",
                PROBE_LOG,
                best->addr.addr[0], best->addr.addr[1], best->addr.addr[2],
                best->addr.addr[3], best->addr.addr[4], best->addr.addr[5],
                best->rssi);
    sle_remove_paired_remote_device(&g_target_addr);
    sle_connect_remote_device(&g_target_addr);
}

static void probe_power_on_cb(uint8_t status)
{
    osal_printk("%s power on: %d\r\n", PROBE_LOG, status);
    enable_sle();
}

static void probe_enable_cb(uint8_t status)
{
    osal_printk("%s sle enable: %d\r\n", PROBE_LOG, status);
    probe_start_scan();
}

static void probe_seek_enable_cb(errcode_t status)
{
    osal_printk("%s seek enable: 0x%x\r\n", PROBE_LOG, status);
}

static void probe_seek_result_cb(sle_seek_result_info_t *result)
{
    scan_device_t *dev;
    if (result == NULL) {
        return;
    }
    dev = scan_table_find(&result->addr);
    if (dev == NULL) {
        dev = scan_table_add(&result->addr);
    }
    if (dev == NULL) {
        return;
    }
    dev->count++;
    dev->rssi = result->rssi;

    if (uapi_systick_get_ms() - g_scan_start_ms >= PROBE_SCAN_MS) {
        sle_stop_seek();
    }
}

static void probe_seek_disable_cb(errcode_t status)
{
    osal_printk("%s seek disable: 0x%x\r\n", PROBE_LOG, status);
    scan_table_print();
    probe_connect_best();
}

static void probe_conn_state_cb(uint16_t conn_id, const sle_addr_t *addr,
                                sle_acb_state_t state, sle_pair_state_t pair_state,
                                sle_disc_reason_t reason)
{
    (void)addr;
    g_conn_id = conn_id;
    osal_printk("%s conn state: %d pair:%d reason:0x%x\r\n", PROBE_LOG, state, pair_state, reason);
    if (state == SLE_ACB_STATE_CONNECTED) {
        osal_printk("%s connected, conn_id=%u\r\n", PROBE_LOG, conn_id);
        if (pair_state == SLE_PAIR_NONE) {
            sle_pair_remote_device(&g_target_addr);
        }
    } else if (state == SLE_ACB_STATE_DISCONNECTED) {
        scan_table_reset();
        probe_start_scan();
    }
}

static void probe_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    (void)conn_id;
    (void)addr;
    osal_printk("%s pair complete: 0x%x\r\n", PROBE_LOG, status);
}

void probe_init(void)
{
    g_dev_cbk.sle_power_on_cb = probe_power_on_cb;
    g_dev_cbk.sle_enable_cb = probe_enable_cb;
    sle_dev_manager_register_callbacks(&g_dev_cbk);

    g_seek_cbk.seek_enable_cb = probe_seek_enable_cb;
    g_seek_cbk.seek_result_cb = probe_seek_result_cb;
    g_seek_cbk.seek_disable_cb = probe_seek_disable_cb;
    sle_announce_seek_register_callbacks(&g_seek_cbk);

    g_conn_cbk.connect_state_changed_cb = probe_conn_state_cb;
    g_conn_cbk.pair_complete_cb = probe_pair_complete_cb;
    sle_connection_register_callbacks(&g_conn_cbk);
}
