#include "soc_osal.h"
#include "systick.h"
#include "securec.h"
#include "sle_common.h"
#include "sle_device_manager.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_ssap_client.h"
#include "sle_low_latency.h"
#include "scan_table.h"
#include "bs21_util.h"
#include "sle_probe_client.h"

/* *****************************************************************************
 * Macros
 * *****************************************************************************/

#define PROBE_LOG        "[probe]"
#define PROBE_SCAN_MS    5000
#define PROBE_MTU_SIZE_DEFAULT 520

/* *****************************************************************************
 * Global state
 * *****************************************************************************/

static ssapc_callbacks_t g_ssapc_cbk = { 0 };
static sle_dev_manager_callbacks_t g_dev_cbk = { 0 };
static sle_announce_seek_callbacks_t g_seek_cbk = { 0 };
static sle_connection_callbacks_t g_conn_cbk = { 0 };

static sle_addr_t g_target_addr = { 0 };
uint16_t g_conn_id = 0;
static uint64_t g_scan_start_ms = 0;

/* Discovered property handles, grouped by capability. */
static uint16_t g_write_hdls[8];  static uint8_t g_write_cnt = 0;
static uint16_t g_notify_hdls[8]; static uint8_t g_notify_cnt = 0;
static uint16_t g_all_hdls[8];    static uint8_t g_all_cnt = 0;
static uint16_t g_cmd_hdls[8];    static uint8_t g_cmd_cnt = 0;
static uint8_t g_desc_types[8][4]; static uint8_t g_desc_cnt[8];

/* SSAP find sequence: scan all 6 structure types in order. */
static const uint8_t g_find_types[] = {
    SSAP_FIND_TYPE_PROPERTY,
    SSAP_FIND_TYPE_SERVICE_STRUCTURE,
    SSAP_FIND_TYPE_PRIMARY_SERVICE,
    SSAP_FIND_TYPE_REFERENCE_SERVICE,
    SSAP_FIND_TYPE_METHOD,
    SSAP_FIND_TYPE_EVENT,
};
#define FIND_TYPE_COUNT (sizeof(g_find_types) / sizeof(g_find_types[0]))
static uint8_t g_find_idx = 0;

/* Experiment task synchronization. */
static volatile int g_discovery_done = 0;
static osal_task *g_exp_task = NULL;

/* *****************************************************************************
 * Forward declarations
 * *****************************************************************************/

static int exp_task_entry(void *data);
static void probe_start_exp_task(void);

/* *****************************************************************************
 * Helpers
 * *****************************************************************************/

static void probe_print_hex(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        osal_printk("%02x ", buf[i]);
    }
    osal_printk("\r\n");
}

static void probe_log_frame(const char *tag, const ssapc_handle_value_t *data)
{
    osal_printk("%s %s len=%u ", PROBE_LOG, tag, data->data_len);
    probe_print_hex(data->data, data->data_len);
    if (data->data_len >= 3 && data->data[0] == 0x5a && data->data[1] == 0xa5) {
        if (data->data[2] == 0xef) {
            osal_printk("%s *** INPUT STREAM ***\r\n", PROBE_LOG);
        } else {
            osal_printk("%s ACK cmd=0x%02x\r\n", PROBE_LOG, data->data[2]);
        }
    }
}

/* *****************************************************************************
 * Experiment task (runs linear sequence without blocking SLE callbacks)
 * *****************************************************************************/

static int exp_task_entry(void *data)
{
    (void)data;
    uint8_t w[17];
    ssapc_write_param_t wp;

    while (g_discovery_done == 0) {
        osal_msleep(100);
    }
    osal_printk("%s EXP: discovery done, conn_id=%u\r\n", PROBE_LOG, g_conn_id);

    /* 1. Read 0x13 value (baseline). */
    osal_printk("%s EXP: read 0x13\r\n", PROBE_LOG);
    ssapc_read_req(0, g_conn_id, 0x13, SSAP_PROPERTY_TYPE_VALUE);
    osal_msleep(1000);

    /* 2. Enable notifications on 0x11 (write CCC 0x0001). */
    osal_printk("%s EXP: enable notify on 0x11\r\n", PROBE_LOG);
    memset(&wp, 0, sizeof(wp));
    wp.handle = 0x11;
    wp.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    w[0] = 0x01; w[1] = 0x00;
    wp.data = w; wp.data_len = 2;
    ssapc_write_req(0, g_conn_id, &wp);
    osal_msleep(1000);

    /* 3. Listen for 8 seconds (notifications logged by callback). */
    osal_printk("%s EXP: listening 8s\r\n", PROBE_LOG);
    osal_msleep(8000);

    osal_printk("%s EXP: done\r\n", PROBE_LOG);
    return 0;
}

static void probe_start_exp_task(void)
{
    if (g_exp_task != NULL) return;
    osal_kthread_lock();
    g_exp_task = osal_kthread_create(exp_task_entry, NULL, "exp_task", 4096);
    if (g_exp_task != NULL) {
        osal_kthread_set_priority(g_exp_task, 24);
    }
    osal_kthread_unlock();
    if (g_exp_task == NULL) {
        osal_printk("%s exp task create fail\r\n", PROBE_LOG);
    } else {
        osal_printk("%s exp task started\r\n", PROBE_LOG);
    }
}

/* *****************************************************************************
 * Scan / discovery callbacks — names match sle_announce_seek_callbacks_t fields
 * *****************************************************************************/

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
                PROBE_LOG, best->addr.addr[0], best->addr.addr[1], best->addr.addr[2],
                best->addr.addr[3], best->addr.addr[4], best->addr.addr[5], best->rssi);
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

static void probe_sle_enable_cb(uint8_t status)
{
    osal_printk("%s sle_enable_cb: %d\r\n", PROBE_LOG, status);
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
    osal_printk("%s seek_enable_cb: 0x%x\r\n", PROBE_LOG, status);
}

static void probe_seek_result_cb(sle_seek_result_info_t *result)
{
    if (result == NULL) return;
    scan_device_t *dev = scan_table_find(&result->addr);
    if (dev == NULL) dev = scan_table_add(&result->addr);
    if (dev == NULL) return;
    dev->count++;
    dev->rssi = result->rssi;
    osal_printk("%s seek: %02x:%02x:%02x:%02x:%02x:%02x rssi=%d data=",
                PROBE_LOG, result->addr.addr[0], result->addr.addr[1], result->addr.addr[2],
                result->addr.addr[3], result->addr.addr[4], result->addr.addr[5], result->rssi);
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
    osal_printk("%s seek_disable_cb: 0x%x\r\n", PROBE_LOG, status);
    if (status != ERRCODE_SUCC) {
        scan_table_reset();
        probe_start_scan();
        return;
    }
    scan_table_print();
    probe_connect_best();
}

/* *****************************************************************************
 * Connection callbacks — names match sle_connection_callbacks_t fields
 * *****************************************************************************/

static void probe_connect_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
                                           sle_acb_state_t state,
                                           sle_pair_state_t pair_state,
                                           sle_disc_reason_t reason)
{
    g_conn_id = conn_id;
    osal_printk("%s conn state: %d pair:%d reason:0x%x\r\n", PROBE_LOG, state, pair_state, reason);
    if (state == SLE_ACB_STATE_CONNECTED) {
        osal_printk("%s connected, conn_id=%u\r\n", PROBE_LOG, conn_id);
        g_discovery_done = 0;
        g_find_idx = 0;
        g_notify_cnt = 0; g_write_cnt = 0; g_all_cnt = 0; g_cmd_cnt = 0;
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
    (void)conn_id; (void)addr;
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

/* *****************************************************************************
 * SSAP discovery callbacks — names match ssapc_callbacks_t fields
 * *****************************************************************************/

static void ssapc_exchange_info_cb(uint8_t client_id, uint16_t conn_id,
                                   ssap_exchange_info_t *param, errcode_t status)
{
    (void)client_id; (void)conn_id;
    if (status != ERRCODE_SUCC || param == NULL) {
        osal_printk("%s exchange info failed: 0x%x\r\n", PROBE_LOG, status);
        return;
    }
    osal_printk("%s exchange info: mtu=%u\r\n", PROBE_LOG, param->mtu_size);
    g_find_idx = 0;
    ssapc_find_structure_param_t fp = { 0 };
    fp.type = g_find_types[g_find_idx];
    fp.start_hdl = 1;
    fp.end_hdl = 0xFFFF;
    osal_printk("%s find type=%u\r\n", PROBE_LOG, fp.type);
    ssapc_find_structure(0, g_conn_id, &fp);
}

static void ssapc_find_structure_cb(uint8_t client_id, uint16_t conn_id,
                                    ssapc_find_service_result_t *service,
                                    errcode_t status)
{
    (void)client_id; (void)conn_id;
    if (status != ERRCODE_SUCC || service == NULL) return;
    osal_printk("%s find_structure_cb: status=0x%x start=0x%x end=0x%x uuid_len=%u\r\n",
                PROBE_LOG, status, service->start_hdl, service->end_hdl, service->uuid.len);
}

static void ssapc_find_property_cbk(uint8_t client_id, uint16_t conn_id,
                                    ssapc_find_property_result_t *property,
                                    errcode_t status)
{
    (void)client_id; (void)conn_id;
    if (status != ERRCODE_SUCC || property == NULL) return;

    osal_printk("%s find_property: hdl=0x%x oper=0x%x desc_cnt=%u types=[",
                PROBE_LOG, property->handle, property->operate_indication,
                property->descriptors_count);
    for (uint8_t i = 0; i < property->descriptors_count && i < 4; i++) {
        osal_printk("%s%02x", (i > 0) ? "," : "", property->descriptors_type[i]);
    }
    osal_printk("]\r\n");

    if (g_all_cnt < 8) {
        uint8_t idx = g_all_cnt;
        g_all_hdls[idx] = property->handle;
        g_desc_cnt[idx] = (property->descriptors_count < 4) ? property->descriptors_count : 4;
        for (uint8_t i = 0; i < g_desc_cnt[idx]; i++) {
            g_desc_types[idx][i] = property->descriptors_type[i];
        }
        g_all_cnt++;
    }
    if ((property->operate_indication & (SSAP_OPERATE_INDICATION_BIT_WRITE |
                                         SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP)) &&
        g_write_cnt < 8) {
        g_write_hdls[g_write_cnt++] = property->handle;
    }
    if ((property->operate_indication & (SSAP_OPERATE_INDICATION_BIT_NOTIFY |
                                         SSAP_OPERATE_INDICATION_BIT_INDICATE)) &&
        g_notify_cnt < 8) {
        g_notify_hdls[g_notify_cnt++] = property->handle;
    }
    if ((property->operate_indication & SSAP_OPERATE_INDICATION_BIT_WRITE) &&
        g_cmd_cnt < 8) {
        g_cmd_hdls[g_cmd_cnt++] = property->handle;
    }
}

static void ssapc_find_structure_cmp_cb(uint8_t client_id, uint16_t conn_id,
                                        ssapc_find_structure_result_t *result,
                                        errcode_t status)
{
    (void)client_id; (void)conn_id;
    osal_printk("%s find_cmp[%u/%u]: status=0x%x type=%u\r\n",
                PROBE_LOG, g_find_idx + 1, FIND_TYPE_COUNT, status,
                (result != NULL) ? result->type : 0xFF);

    g_find_idx++;
    if (g_find_idx >= FIND_TYPE_COUNT) {
        osal_printk("%s all %u find types done\r\n", PROBE_LOG, FIND_TYPE_COUNT);
        g_discovery_done = 1;
        return;
    }

    ssapc_find_structure_param_t fp = { 0 };
    fp.type = g_find_types[g_find_idx];
    fp.start_hdl = 1;
    fp.end_hdl = 0xFFFF;
    osal_printk("%s find type=%u\r\n", PROBE_LOG, fp.type);
    ssapc_find_structure(0, g_conn_id, &fp);
}

/* *****************************************************************************
 * SSAP data callbacks — names match ssapc_callbacks_t fields
 * *****************************************************************************/

static void ssapc_write_cfm_cb(uint8_t client_id, uint16_t conn_id,
                               ssapc_write_result_t *write_result, errcode_t status)
{
    (void)client_id; (void)conn_id;
    osal_printk("%s write_cfm: status=0x%x handle=0x%x type=0x%02x\r\n",
                PROBE_LOG, status, write_result->handle, write_result->type);
}

static void ssapc_read_cfm_cb(uint8_t client_id, uint16_t conn_id,
                              ssapc_handle_value_t *read_data, errcode_t status)
{
    (void)client_id; (void)conn_id;
    if (status != ERRCODE_SUCC || read_data == NULL) {
        osal_printk("%s read_cfm: fail status=0x%x\r\n", PROBE_LOG, status);
        return;
    }
    osal_printk("%s read_cfm: handle=0x%x type=0x%02x len=%u ",
                PROBE_LOG, read_data->handle, read_data->type, read_data->data_len);
    probe_print_hex(read_data->data, read_data->data_len);
}

static void ssapc_notification_cb(uint8_t client_id, uint16_t conn_id,
                                  ssapc_handle_value_t *data, errcode_t status)
{
    (void)client_id; (void)conn_id; (void)status;
    probe_log_frame("notification", data);
}

static void ssapc_indication_cb(uint8_t client_id, uint16_t conn_id,
                                ssapc_handle_value_t *data, errcode_t status)
{
    (void)client_id; (void)conn_id; (void)status;
    probe_log_frame("indication", data);
}

/* *****************************************************************************
 * Low-latency RX callback
 * *****************************************************************************/

static void low_latency_rx_cb(uint16_t len, uint8_t *value)
{
    if (value == NULL || len == 0) return;
    osal_printk("%s low_latency_rx: len=%u ", PROBE_LOG, len);
    probe_print_hex(value, len);
}

/* *****************************************************************************
 * Init
 * *****************************************************************************/

void probe_init(void)
{
    /* Device manager callbacks. */
    g_dev_cbk.sle_power_on_cb = probe_power_on_cb;
    g_dev_cbk.sle_enable_cb   = probe_sle_enable_cb;
    sle_dev_manager_register_callbacks(&g_dev_cbk);

    /* Announce / seek callbacks. */
    g_seek_cbk.seek_enable_cb   = probe_seek_enable_cb;
    g_seek_cbk.seek_result_cb   = probe_seek_result_cb;
    g_seek_cbk.seek_disable_cb  = probe_seek_disable_cb;
    sle_announce_seek_register_callbacks(&g_seek_cbk);

    /* Connection callbacks. */
    g_conn_cbk.connect_state_changed_cb = probe_connect_state_changed_cb;
    g_conn_cbk.pair_complete_cb         = probe_pair_complete_cb;
    sle_connection_register_callbacks(&g_conn_cbk);

    /* SSAP client callbacks. */
    g_ssapc_cbk.exchange_info_cb       = ssapc_exchange_info_cb;
    g_ssapc_cbk.find_structure_cb      = ssapc_find_structure_cb;
    g_ssapc_cbk.ssapc_find_property_cbk = ssapc_find_property_cbk;
    g_ssapc_cbk.find_structure_cmp_cb  = ssapc_find_structure_cmp_cb;
    g_ssapc_cbk.write_cfm_cb           = ssapc_write_cfm_cb;
    g_ssapc_cbk.read_cfm_cb            = ssapc_read_cfm_cb;
    g_ssapc_cbk.notification_cb        = ssapc_notification_cb;
    g_ssapc_cbk.indication_cb          = ssapc_indication_cb;
    ssapc_register_callbacks(&g_ssapc_cbk);

    /* Low-latency RX callback. */
    sle_low_latency_rx_callbacks_t ll_cbk = { 0 };
    ll_cbk.low_latency_rx_cb = low_latency_rx_cb;
    sle_low_latency_rx_register_callbacks(&ll_cbk);

    /* Start the experiment task early; it polls g_discovery_done internally. */
    probe_start_exp_task();
}