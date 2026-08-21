#include "soc_osal.h"
#include "systick.h"
#include "securec.h"
#include "sle_common.h"
#include "sle_device_manager.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_ssap_client.h"
#include "scan_table.h"
#include "bs21_util.h"
#include "sle_probe_client.h"

#define PROBE_LOG        "[probe]"
#define PROBE_SCAN_MS    5000
#define PROBE_MTU_SIZE_DEFAULT 520

static ssapc_callbacks_t g_ssapc_cbk = { 0 };

static sle_dev_manager_callbacks_t g_dev_cbk = { 0 };
static sle_announce_seek_callbacks_t g_seek_cbk = { 0 };
static sle_connection_callbacks_t g_conn_cbk = { 0 };
static sle_addr_t g_target_addr = { 0 };
static uint16_t g_conn_id = 0;
static uint64_t g_scan_start_ms = 0;
static uint16_t g_write_hdls[8];
static uint8_t g_write_cnt = 0;

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
    if (sle_connect_remote_device(&g_target_addr) != ERRCODE_SUCC) {
        osal_printk("%s connect fail, rescan\r\n", PROBE_LOG);
        scan_table_reset();
        probe_start_scan();
    }
}

static void probe_power_on_cb(uint8_t status)
{
    osal_printk("%s power on: %d\r\n", PROBE_LOG, status);
    enable_sle();
}

static void probe_enable_cb(uint8_t status)
{
    osal_printk("%s sle enable: %d\r\n", PROBE_LOG, status);
    if (status == 0) {
        sle_addr_t la = { 0 };
        if (sle_get_local_addr(&la) == ERRCODE_SUCC) {
            osal_printk("%s local addr: %02x:%02x:%02x:%02x:%02x:%02x type:%d\r\n",
                        PROBE_LOG, la.addr[0], la.addr[1], la.addr[2],
                        la.addr[3], la.addr[4], la.addr[5], la.type);
        }
        sle_setup_set_local_addr();
        probe_start_scan();
    }
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

    osal_printk("%s seek: %02x:%02x:%02x:%02x:%02x:%02x rssi=%d data=",
                PROBE_LOG,
                result->addr.addr[0], result->addr.addr[1], result->addr.addr[2],
                result->addr.addr[3], result->addr.addr[4], result->addr.addr[5],
                result->rssi);
    for (uint16_t i = 0; i < result->data_length && i < 31; i++) {
        osal_printk("%02x ", result->data[i]);
    }
    osal_printk("\r\n");

    if (uapi_systick_get_ms() - g_scan_start_ms >= PROBE_SCAN_MS) {
        sle_stop_seek();
    }
}

static void probe_seek_disable_cb(errcode_t status)
{
    osal_printk("%s seek disable: 0x%x\r\n", PROBE_LOG, status);
    if (status != ERRCODE_SUCC) {
        scan_table_reset();
        probe_start_scan();
        return;
    }
    scan_table_print();
    probe_connect_best();
}

static void probe_conn_state_cb(uint16_t conn_id, const sle_addr_t *addr,
                                sle_acb_state_t state, sle_pair_state_t pair_state,
                                sle_disc_reason_t reason)
{
    g_conn_id = conn_id;
    osal_printk("%s conn state: %d pair:%d reason:0x%x\r\n", PROBE_LOG, state, pair_state, reason);
    if (state == SLE_ACB_STATE_CONNECTED) {
        osal_printk("%s connected, conn_id=%u\r\n", PROBE_LOG, conn_id);
        if (pair_state == SLE_PAIR_NONE) {
            const sle_addr_t *pair_addr = (addr != NULL) ? addr : &g_target_addr;
            sle_pair_remote_device(pair_addr);
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
    if (status != ERRCODE_SUCC) {
        osal_printk("%s pair failed, rescan\r\n", PROBE_LOG);
        scan_table_reset();
        probe_start_scan();
        return;
    }
    ssap_exchange_info_t info = { 0 };
    info.mtu_size = PROBE_MTU_SIZE_DEFAULT;
    info.version = 1;
    ssapc_exchange_info_req(0, g_conn_id, &info);
}

static void probe_exchange_info_cb(uint8_t client_id, uint16_t conn_id,
                                   ssap_exchange_info_t *param, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    if (status != ERRCODE_SUCC || param == NULL) {
        osal_printk("%s exchange info failed: 0x%x\r\n", PROBE_LOG, status);
        return;
    }
    osal_printk("%s exchange info: 0x%x mtu=%u\r\n", PROBE_LOG, status, param->mtu_size);
    ssapc_find_structure_param_t find_param = { 0 };
    find_param.type = SSAP_FIND_TYPE_PROPERTY;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xFFFF;
    ssapc_find_structure(0, g_conn_id, &find_param);
}

static void probe_find_service_cb(uint8_t client_id, uint16_t conn_id,
                                  ssapc_find_service_result_t *service, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    if (status != ERRCODE_SUCC || service == NULL) {
        osal_printk("%s find service: status=0x%x\r\n", PROBE_LOG, status);
        return;
    }
    osal_printk("%s find service: status=0x%x start=0x%x end=0x%x uuid_len=%u\r\n",
                PROBE_LOG, status, service->start_hdl, service->end_hdl, service->uuid.len);
}

static void probe_find_property_cb(uint8_t client_id, uint16_t conn_id,
                                    ssapc_find_property_result_t *property, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    if (status != ERRCODE_SUCC || property == NULL) {
        osal_printk("%s find property: status=0x%x\r\n", PROBE_LOG, status);
        return;
    }
    osal_printk("%s find property: status=0x%x handle=0x%x oper_ind=%u desc=%u uuid=",
                PROBE_LOG, status, property->handle, property->operate_indication,
                property->descriptors_count);
    for (uint8_t i = 0; i < property->uuid.len && i < 16; i++) {
        osal_printk("%02x ", property->uuid.uuid[i]);
    }
    osal_printk("\r\n");
    if ((property->operate_indication & (SSAP_OPERATE_INDICATION_BIT_WRITE |
                                         SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP)) &&
        g_write_cnt < 8) {
        g_write_hdls[g_write_cnt++] = property->handle;
    }
}

static void probe_print_hex(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        osal_printk("%02x ", buf[i]);
    }
    osal_printk("\r\n");
}

static void probe_write_cfm_cb(uint8_t client_id, uint16_t conn_id,
                                ssapc_write_result_t *write_result, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    osal_printk("%s write cfm: status=0x%x handle=0x%x\r\n",
                PROBE_LOG, status, write_result->handle);
}

static void probe_read_cfm_cb(uint8_t client_id, uint16_t conn_id,
                              ssapc_handle_value_t *read_data, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    if (status != ERRCODE_SUCC || read_data == NULL) {
        osal_printk("%s read cfm: status=0x%x\r\n", PROBE_LOG, status);
        return;
    }
    osal_printk("%s read cfm: status=0x%x handle=0x%x len=%u ",
                PROBE_LOG, status, read_data->handle, read_data->data_len);
    probe_print_hex(read_data->data, read_data->data_len);
}

static void probe_find_cmp_cb(uint8_t client_id, uint16_t conn_id,
                              ssapc_find_structure_result_t *result, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    if (status != ERRCODE_SUCC || result == NULL) {
        osal_printk("%s find complete: status=0x%x\r\n", PROBE_LOG, status);
        return;
    }
    osal_printk("%s find complete: status=0x%x type=%u uuid_len=%u\r\n",
                PROBE_LOG, status, result->type, result->uuid.len);
    uint8_t en[2] = { 0x01, 0x00 };
    for (uint16_t h = 0x12; h <= 0x19; h++) {
        ssapc_write_param_t wp = { 0 };
        wp.handle = h;
        wp.type = SSAP_PROPERTY_TYPE_VALUE;
        wp.data_len = sizeof(en);
        wp.data = en;
        if (ssapc_write_req(0, g_conn_id, &wp) == ERRCODE_SUCC) {
            osal_printk("%s enable notify on 0x%x\r\n", PROBE_LOG, h);
        }
    }
    for (uint8_t i = 0; i < g_write_cnt; i++) {
        uint8_t cmd[1] = { 0x01 };
        ssapc_write_param_t wp = { 0 };
        wp.handle = g_write_hdls[i];
        wp.type = SSAP_PROPERTY_TYPE_VALUE;
        wp.data_len = sizeof(cmd);
        wp.data = cmd;
        if (ssapc_write_req(0, g_conn_id, &wp) == ERRCODE_SUCC) {
            osal_printk("%s write cmd 0x01 to 0x%x\r\n", PROBE_LOG, g_write_hdls[i]);
        }
    }
    uint8_t trial[][1] = {{0x03}, {0x05}, {0x08}, {0x02}};
    for (uint8_t t = 0; t < 4; t++) {
        for (uint8_t i = 0; i < g_write_cnt; i++) {
            ssapc_write_param_t wp = { 0 };
            wp.handle = g_write_hdls[i];
            wp.type = SSAP_PROPERTY_TYPE_VALUE;
            wp.data_len = 1;
            wp.data = trial[t];
            if (ssapc_write_req(0, g_conn_id, &wp) == ERRCODE_SUCC) {
                osal_printk("%s write cmd 0x%02x to 0x%x\r\n", PROBE_LOG, trial[t][0], g_write_hdls[i]);
            }
        }
    }
    for (uint16_t h = 0x11; h <= 0x18; h++) {
        if (ssapc_read_req(0, g_conn_id, h, 0) == ERRCODE_SUCC) {
            osal_printk("%s read 0x%x\r\n", PROBE_LOG, h);
        }
    }
}

static void probe_notification_cb(uint8_t client_id, uint16_t conn_id,
                                  ssapc_handle_value_t *data, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    (void)status;
    osal_printk("%s recv len=%u ", PROBE_LOG, data->data_len);
    probe_print_hex(data->data, data->data_len);
}

static void probe_indication_cb(uint8_t client_id, uint16_t conn_id,
                                ssapc_handle_value_t *data, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    (void)status;
    osal_printk("%s ind len=%u ", PROBE_LOG, data->data_len);
    probe_print_hex(data->data, data->data_len);
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

    g_ssapc_cbk.exchange_info_cb = probe_exchange_info_cb;
    g_ssapc_cbk.find_structure_cb = probe_find_service_cb;
    g_ssapc_cbk.ssapc_find_property_cbk = probe_find_property_cb;
    g_ssapc_cbk.find_structure_cmp_cb = probe_find_cmp_cb;
    g_ssapc_cbk.write_cfm_cb = probe_write_cfm_cb;
    g_ssapc_cbk.read_cfm_cb = probe_read_cfm_cb;
    g_ssapc_cbk.notification_cb = probe_notification_cb;
    g_ssapc_cbk.indication_cb = probe_indication_cb;
    ssapc_register_callbacks(&g_ssapc_cbk);
}
