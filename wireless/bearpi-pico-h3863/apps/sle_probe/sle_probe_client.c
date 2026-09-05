#include "scan_table.h"
#include "securec.h"
#include "soc_osal.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_probe_client.h"
#include "sle_ssap_client.h"
#include "systick.h"

#define PROBE_LOG "[probe]"

#define PROBE_SEEK_INTERVAL_DEFAULT 100
#define PROBE_SEEK_WINDOW_DEFAULT 100
#define PROBE_MTU_SIZE_DEFAULT 520

/* Dongle matches the controller broadcast name, then connects (official RCU
 * dongle behavior). */
#define PROBE_TARGET_NAME "fly_digig1"

#define PROBE_MAX_PROPERTIES 16

static sle_addr_t g_target_addr = {0};
static uint16_t g_conn_id = 0;
static uint8_t g_client_id = 0;

static uint16_t g_prop_hdls[PROBE_MAX_PROPERTIES];
static uint8_t g_prop_cnt = 0;
static uint8_t g_read_idx = 0;
static uint8_t g_find_phase = 0;

static void probe_start_scan(void);
static void probe_rescan(void);
static void probe_start_next_read(void);
static void probe_start_ssap(void);
static void probe_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status);

static bool probe_has_name(const uint8_t *data, uint16_t len, const char *name) {
    uint16_t name_len = strlen(name);
    for (uint16_t i = 0; i + name_len <= len; i++) {
        if (memcmp(&data[i], name, name_len) == 0) {
            return true;
        }
    }
    return false;
}

static void probe_print_hex(const uint8_t *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        osal_printk("%02x ", buf[i]);
    }
    osal_printk("\r\n");
}

static void probe_start_scan(void) {
    sle_seek_param_t param = {0};
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = 1;
    param.seek_type[0] = 1;
    param.seek_interval[0] = PROBE_SEEK_INTERVAL_DEFAULT;
    param.seek_window[0] = PROBE_SEEK_WINDOW_DEFAULT;
    sle_set_seek_param(&param);
    sle_start_seek();
    osal_printk("%s start scan...\r\n", PROBE_LOG);
}

static void probe_rescan(void) {
    g_prop_cnt = 0;
    g_read_idx = 0;
    g_find_phase = 0;
    scan_table_reset();
    probe_start_scan();
}

static void probe_sle_enable_cb(errcode_t status) {
    osal_printk("%s sle enable: %x\r\n", PROBE_LOG, status);
    if (status == ERRCODE_SLE_SUCCESS) {
        probe_start_scan();
    }
}

static void probe_seek_enable_cb(errcode_t status) {
    osal_printk("%s seek enable: %x\r\n", PROBE_LOG, status);
}

static void probe_seek_result_cb(sle_seek_result_info_t *result) {
    if (result == NULL) {
        return;
    }
    osal_printk(
        "%s seek: evt:%02x addr:%02x:%02x:%02x:%02x:%02x:%02x type:%d rssi:%d len:%d data: ",
        PROBE_LOG,
        result->event_type,
        result->addr.addr[0],
        result->addr.addr[1],
        result->addr.addr[2],
        result->addr.addr[3],
        result->addr.addr[4],
        result->addr.addr[5],
        result->addr.type,
        result->rssi,
        result->data_length
    );
    probe_print_hex(result->data, result->data_length);

    scan_device_t *dev = scan_table_find(&result->addr);
    if (dev == NULL) {
        dev = scan_table_add(&result->addr);
    }
    if (dev != NULL) {
        dev->count++;
        dev->rssi = result->rssi;
    }

    if (probe_has_name(result->data, result->data_length, PROBE_TARGET_NAME)) {
        osal_printk("%s found target, stopping scan...\r\n", PROBE_LOG);
        memcpy_s(&g_target_addr, sizeof(sle_addr_t), &result->addr, sizeof(sle_addr_t));
        sle_stop_seek();
    }
}

static void probe_seek_disable_cb(errcode_t status) {
    osal_printk("%s seek disable: %x\r\n", PROBE_LOG, status);
    if (status != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s seek failed, rescan\r\n", PROBE_LOG);
        scan_table_print();
        probe_rescan();
        return;
    }
    if (g_target_addr.addr[0] == 0) {
        osal_printk("%s target not found, rescan\r\n", PROBE_LOG);
        probe_rescan();
        return;
    }
    osal_printk(
        "%s connecting %02x:%02x:%02x:%02x:%02x:%02x\r\n",
        PROBE_LOG,
        g_target_addr.addr[0],
        g_target_addr.addr[1],
        g_target_addr.addr[2],
        g_target_addr.addr[3],
        g_target_addr.addr[4],
        g_target_addr.addr[5]
    );
    errcode_t ret = sle_remove_paired_remote_device(&g_target_addr);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s remove paired failed 0x%x (non-fatal)\r\n", PROBE_LOG, ret);
    }
    ret = sle_connect_remote_device(&g_target_addr);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s connect failed 0x%x, rescan\r\n", PROBE_LOG, ret);
        probe_rescan();
    }
}

static void probe_connect_state_changed_cb(
    uint16_t conn_id,
    const sle_addr_t *addr,
    sle_acb_state_t state,
    sle_pair_state_t pair_state,
    sle_disc_reason_t reason
) {
    g_conn_id = conn_id;
    osal_printk("%s conn state:%d pair:%d reason:%x\r\n", PROBE_LOG, state, pair_state, reason);
    if (state == SLE_ACB_STATE_CONNECTED) {
        osal_printk("%s connected conn_id=%u\r\n", PROBE_LOG, conn_id);
        if (pair_state == SLE_PAIR_NONE) {
            const sle_addr_t *pair_addr = (addr != NULL) ? addr : &g_target_addr;
            errcode_t ret = sle_pair_remote_device(pair_addr);
            if (ret != ERRCODE_SLE_SUCCESS) {
                osal_printk("%s pair req failed 0x%x\r\n", PROBE_LOG, ret);
                probe_pair_complete_cb(conn_id, pair_addr, ret);
            }
        } else {
            /* Already paired, go straight to SSAP exchange. */
            probe_start_ssap();
        }
    } else if (state == SLE_ACB_STATE_DISCONNECTED) {
        osal_printk("%s disconnected, rescan\r\n", PROBE_LOG);
        probe_rescan();
    }
}

static void probe_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status) {
    osal_printk("%s pair complete: conn=%u status=%x\r\n", PROBE_LOG, conn_id, status);
    if (status != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s pair failed, rescan\r\n", PROBE_LOG);
        probe_rescan();
        return;
    }
    probe_start_ssap();
}

static void probe_start_ssap(void) {
    ssap_exchange_info_t info = {0};
    info.mtu_size = PROBE_MTU_SIZE_DEFAULT;
    info.version = 1;
    errcode_t ret = ssapc_exchange_info_req(g_client_id, g_conn_id, &info);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s exchange_info req failed 0x%x\r\n", PROBE_LOG, ret);
    }
}

static void probe_start_next_find(void) {
    uint8_t type;
    if (g_find_phase == 0) {
        type = SSAP_FIND_TYPE_PRIMARY_SERVICE;
    } else {
        type = SSAP_FIND_TYPE_PROPERTY;
    }
    ssapc_find_structure_param_t find_param = {0};
    find_param.type = type;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xFFFF;
    errcode_t ret = ssapc_find_structure(g_client_id, g_conn_id, &find_param);
    osal_printk("%s find structure type=%u sent ret=%x\r\n", PROBE_LOG, type, ret);
}

static void ssapc_exchange_info_cb(
    uint8_t client_id, uint16_t conn_id, ssap_exchange_info_t *param, errcode_t status
) {
    g_client_id = client_id;
    osal_printk(
        "%s exchange_info mtu=%d ver=%d status=%x\r\n",
        PROBE_LOG,
        (param != NULL) ? param->mtu_size : 0,
        (param != NULL) ? param->version : 0,
        status
    );
    if (status != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s exchange failed, rescan\r\n", PROBE_LOG);
        probe_rescan();
        return;
    }

    g_find_phase = 0;
    probe_start_next_find();
}

static void ssapc_find_structure_cb(
    uint8_t client_id, uint16_t conn_id, ssapc_find_service_result_t *service, errcode_t status
) {
    if (status != ERRCODE_SLE_SUCCESS || service == NULL) {
        return;
    }
    osal_printk(
        "%s find_structure hdl=%x-%x uuid[len=%u]:",
        PROBE_LOG,
        service->start_hdl,
        service->end_hdl,
        service->uuid.len
    );
    for (uint8_t i = 0; i < SLE_UUID_LEN; i++) {
        osal_printk("%02X", service->uuid.uuid[i]);
    }
    osal_printk(" status=%x\r\n", status);
}

static void ssapc_find_property_cb(
    uint8_t client_id, uint16_t conn_id, ssapc_find_property_result_t *property, errcode_t status
) {
    if (status != ERRCODE_SLE_SUCCESS || property == NULL) {
        return;
    }
    osal_printk(
        "%s find_property hdl=0x%x oper=0x%x desc_cnt=%u uuid[len=%u]:",
        PROBE_LOG,
        property->handle,
        property->operate_indication,
        property->descriptors_count,
        property->uuid.len
    );
    for (uint8_t i = 0; i < SLE_UUID_LEN; i++) {
        osal_printk("%02X", property->uuid.uuid[i]);
    }
    osal_printk("\r\n");
    if (g_prop_cnt < PROBE_MAX_PROPERTIES) {
        g_prop_hdls[g_prop_cnt++] = property->handle;
    }
}

static void ssapc_find_structure_cmp_cb(
    uint8_t client_id, uint16_t conn_id, ssapc_find_structure_result_t *result, errcode_t status
) {
    osal_printk(
        "%s find complete phase=%u status=%x, %u properties\r\n",
        PROBE_LOG,
        g_find_phase,
        status,
        g_prop_cnt
    );
    if (g_find_phase == 0) {
        g_find_phase = 1;
        g_prop_cnt = 0;
        probe_start_next_find();
        return;
    }
    g_read_idx = 0;
    probe_start_next_read();
}

static void probe_start_next_read(void) {
    if (g_read_idx >= g_prop_cnt) {
        osal_printk("%s all reads done (%u properties)\r\n", PROBE_LOG, g_prop_cnt);
        sle_connection_param_update_t params = {0};
        params.conn_id = g_conn_id;
        params.interval_min = 0x64;
        params.interval_max = 0x64;
        params.max_latency = 0x3;
        params.supervision_timeout = 0x1F4;
        errcode_t ret = sle_update_connect_param(&params);
        osal_printk("%s update_connect_param ret=%x\r\n", PROBE_LOG, ret);
        return;
    }
    uint16_t hdl = g_prop_hdls[g_read_idx];
    errcode_t ret = ssapc_read_req(g_client_id, g_conn_id, hdl, SSAP_PROPERTY_TYPE_VALUE);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s read hdl=0x%x rejected 0x%x\r\n", PROBE_LOG, hdl, ret);
        g_read_idx++;
        probe_start_next_read();
    }
}

static void ssapc_read_cfm_cb(
    uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *read_data, errcode_t status
) {
    if (status != ERRCODE_SLE_SUCCESS || read_data == NULL) {
        osal_printk("%s read_cfm fail status=%x\r\n", PROBE_LOG, status);
        g_read_idx++;
        probe_start_next_read();
        return;
    }
    /* SDK does not fill read_data->handle; track via request order. */
    uint16_t hdl = (g_read_idx < g_prop_cnt) ? g_prop_hdls[g_read_idx] : 0;
    osal_printk("%s read hdl=0x%x len=%u data: ", PROBE_LOG, hdl, read_data->data_len);
    probe_print_hex(read_data->data, read_data->data_len);
    g_read_idx++;
    probe_start_next_read();
}

static void ssapc_notification_cb(
    uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data, errcode_t status
) {
    osal_printk("%s notification hdl=0x%x len=%u data: ", PROBE_LOG, data->handle, data->data_len);
    probe_print_hex(data->data, data->data_len);
}

static void ssapc_indication_cb(
    uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data, errcode_t status
) {
    osal_printk("%s indication hdl=0x%x len=%u data: ", PROBE_LOG, data->handle, data->data_len);
    probe_print_hex(data->data, data->data_len);
}

errcode_t probe_init(void) {
    sle_announce_seek_callbacks_t seek_cbk = {0};
    seek_cbk.sle_enable_cb = probe_sle_enable_cb;
    seek_cbk.seek_enable_cb = probe_seek_enable_cb;
    seek_cbk.seek_result_cb = probe_seek_result_cb;
    seek_cbk.seek_disable_cb = probe_seek_disable_cb;
    errcode_t ret = sle_announce_seek_register_callbacks(&seek_cbk);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s seek cbk fail:%x\r\n", PROBE_LOG, ret);
        return ret;
    }

    sle_connection_callbacks_t conn_cbk = {0};
    conn_cbk.connect_state_changed_cb = probe_connect_state_changed_cb;
    conn_cbk.pair_complete_cb = probe_pair_complete_cb;
    ret = sle_connection_register_callbacks(&conn_cbk);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s conn cbk fail:%x\r\n", PROBE_LOG, ret);
        return ret;
    }

    ssapc_callbacks_t ssapc_cbk = {0};
    ssapc_cbk.exchange_info_cb = ssapc_exchange_info_cb;
    ssapc_cbk.find_structure_cb = ssapc_find_structure_cb;
    ssapc_cbk.ssapc_find_property_cbk = ssapc_find_property_cb;
    ssapc_cbk.find_structure_cmp_cb = ssapc_find_structure_cmp_cb;
    ssapc_cbk.read_cfm_cb = ssapc_read_cfm_cb;
    ssapc_cbk.notification_cb = ssapc_notification_cb;
    ssapc_cbk.indication_cb = ssapc_indication_cb;
    ret = ssapc_register_callbacks(&ssapc_cbk);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssapc cbk fail:%x\r\n", PROBE_LOG, ret);
        return ret;
    }

    if (enable_sle() != ERRCODE_SUCC) {
        osal_printk("%s enable_sle fail!\r\n", PROBE_LOG);
        return -1;
    }

    osal_printk("%s init ok\r\n", PROBE_LOG);
    return ERRCODE_SLE_SUCCESS;
}