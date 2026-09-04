#include "securec.h"
#include "soc_osal.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_scan.h"

#define SCAN_LOG "[scan]"

/* Cycle the scan PHY at runtime so a single capture measures which PHY each
 * broadcaster is actually transmitted on. */
static const uint8_t PHY_CYCLE[] = {0x1, 0x2, 0x4}; /* 1M, 2M, 4M */
#define PHY_CYCLE_LEN 3

static volatile uint8_t g_phy_idx = 0;

static void scan_print_hex(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        osal_printk("%02x ", data[i]);
    }
}

static void scan_seek_result_cb(sle_seek_result_info_t *result) {
    if (result == NULL) {
        return;
    }
    osal_printk(
        "%s phy=0x%x adv %02x:%02x:%02x:%02x:%02x:%02x evt:0x%02x rssi:%d dstat:%d len:%d data: ",
        SCAN_LOG, PHY_CYCLE[g_phy_idx], result->addr.addr[0], result->addr.addr[1],
        result->addr.addr[2], result->addr.addr[3], result->addr.addr[4], result->addr.addr[5],
        result->event_type, result->rssi, result->data_status, result->data_length);
    scan_print_hex(result->data, result->data_length);
    osal_printk("\r\n");
}

static void scan_seek_enable_cb(errcode_t status) {
    osal_printk("%s seek enable phy=0x%x: 0x%x\r\n", SCAN_LOG, PHY_CYCLE[g_phy_idx], status);
}

static void scan_seek_disable_cb(errcode_t status) {
    osal_printk("%s seek disable phy=0x%x: 0x%x\r\n", SCAN_LOG, PHY_CYCLE[g_phy_idx], status);
}

static void scan_start_seek(uint8_t phy) {
    sle_seek_param_t param = {0};
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = phy;
    param.seek_type[0] = 1;
    param.seek_interval[0] = 100;
    param.seek_window[0] = 100;
    errcode_t ret = sle_set_seek_param(&param);
    osal_printk("%s >>> set_seek_param(phy=0x%x) ret=0x%x\r\n", SCAN_LOG, phy, ret);
    ret = sle_start_seek();
    osal_printk("%s >>> start_seek(phy=0x%x) ret=0x%x\r\n", SCAN_LOG, phy, ret);
}

/* Rotate the scan PHY; called from sle_scan task every few seconds. */
void scan_phy_cycle(void) {
    sle_stop_seek();
    osal_msleep(200);
    g_phy_idx = (uint8_t)((g_phy_idx + 1) % PHY_CYCLE_LEN);
    scan_start_seek(PHY_CYCLE[g_phy_idx]);
}

static void scan_sle_enable_cb(errcode_t status) {
    osal_printk("%s sle enable: 0x%x\r\n", SCAN_LOG, status);
    if (status == ERRCODE_SLE_SUCCESS) {
        scan_start_seek(PHY_CYCLE[g_phy_idx]);
    }
}

errcode_t sle_scan_init(void) {
    sle_announce_seek_callbacks_t seek_cbk = {0};
    seek_cbk.sle_enable_cb = scan_sle_enable_cb;
    seek_cbk.seek_enable_cb = scan_seek_enable_cb;
    seek_cbk.seek_disable_cb = scan_seek_disable_cb;
    seek_cbk.seek_result_cb = scan_seek_result_cb;
    errcode_t ret = sle_announce_seek_register_callbacks(&seek_cbk);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register cbk fail:0x%x\r\n", SCAN_LOG, ret);
        return ret;
    }
    if (enable_sle() != ERRCODE_SUCC) {
        osal_printk("%s enable_sle fail\r\n", SCAN_LOG);
        return -1;
    }
    osal_printk("%s init ok\r\n", SCAN_LOG);
    return ERRCODE_SLE_SUCCESS;
}