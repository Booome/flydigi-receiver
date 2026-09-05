#include "securec.h"
#include "soc_osal.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_ssap_client.h"
#include "sle_errcode.h"
#include "sle_client.h"

#define SLE_MTU_SIZE 520
#define SLE_SEEK_INTERVAL_DEFAULT 100
#define SLE_SEEK_WINDOW_DEFAULT 100

#define SLE_CLIENT_LOG "[sle client]"

#define SLE_TARGET_NAME "fly_digig1"

static uint16_t g_conn_id = 0;
static uint16_t g_property_handle = 0;
static sle_addr_t g_remote_addr = {0};
static uint8_t g_client_id = 0;

static void sle_client_start_scan(void) {
    sle_seek_param_t param = {0};
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = 1;
    param.seek_type[0] = 1;
    param.seek_interval[0] = SLE_SEEK_INTERVAL_DEFAULT;
    param.seek_window[0] = SLE_SEEK_WINDOW_DEFAULT;
    sle_set_seek_param(&param);
    sle_start_seek();
    osal_printk("%s start scan...\r\n", SLE_CLIENT_LOG);
}

static void sle_client_sle_enable_cbk(errcode_t status) {
    osal_printk("%s sle enable status: %d\r\n", SLE_CLIENT_LOG, status);
    sle_client_start_scan();
}

static void sle_client_seek_enable_cbk(errcode_t status) {
    if (status != 0) {
        osal_printk("%s seek enable error, status=%x\r\n", SLE_CLIENT_LOG, status);
    }
}

static bool sle_client_has_name(const uint8_t *data, uint16_t len, const char *name) {
    uint16_t name_len = strlen(name);
    for (uint16_t i = 0; i + name_len <= len; i++) {
        if (memcmp(&data[i], name, name_len) == 0) {
            return true;
        }
    }
    return false;
}

static void sle_client_seek_result_cbk(sle_seek_result_info_t *seek_result_data) {
    if (seek_result_data == NULL) {
        osal_printk("%s seek result NULL\r\n", SLE_CLIENT_LOG);
        return;
    }

    osal_printk("%s scan addr:%02x:%02x:%02x:%02x:%02x:%02x rssi:%d len:%d data:", SLE_CLIENT_LOG,
                seek_result_data->addr.addr[0], seek_result_data->addr.addr[1],
                seek_result_data->addr.addr[2], seek_result_data->addr.addr[3],
                seek_result_data->addr.addr[4], seek_result_data->addr.addr[5],
                seek_result_data->rssi, seek_result_data->data_length);
    for (uint8_t i = 0; i < seek_result_data->data_length; i++) {
        osal_printk("%02x ", seek_result_data->data[i]);
    }
    osal_printk("\r\n");

    if (sle_client_has_name(seek_result_data->data, seek_result_data->data_length,
                            SLE_TARGET_NAME)) {
        osal_printk("%s found target, stopping scan...\r\n", SLE_CLIENT_LOG);
        memcpy_s(&g_remote_addr, sizeof(sle_addr_t), &seek_result_data->addr, sizeof(sle_addr_t));
        sle_stop_seek();
    }
}

static void sle_client_seek_disable_cbk(errcode_t status) {
    osal_printk("%s scan stopped, status=%x\r\n", SLE_CLIENT_LOG, status);
    if (status == 0) {
        sle_remove_paired_remote_device(&g_remote_addr);
        osal_printk("%s connecting...\r\n", SLE_CLIENT_LOG);
        sle_connect_remote_device(&g_remote_addr);
    }
}

static void sle_client_connect_state_changed_cbk(uint16_t conn_id, const sle_addr_t *addr,
                                                 sle_acb_state_t conn_state,
                                                 sle_pair_state_t pair_state,
                                                 sle_disc_reason_t disc_reason) {
    osal_printk("%s conn_state conn_id:0x%02x, state:0x%x, pair:0x%x\r\n", SLE_CLIENT_LOG, conn_id,
                conn_state, pair_state);

    g_conn_id = conn_id;

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        osal_printk("%s CONNECTED\r\n", SLE_CLIENT_LOG);
        if (pair_state == SLE_PAIR_NONE) {
            osal_printk("%s pairing...\r\n", SLE_CLIENT_LOG);
            sle_pair_remote_device(&g_remote_addr);
        }
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        osal_printk("%s DISCONNECTED, re-scan\r\n", SLE_CLIENT_LOG);
        sle_remove_paired_remote_device(&g_remote_addr);
        g_conn_id = 0;
        sle_client_start_scan();
    }
}

static void sle_client_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr,
                                         errcode_t status) {
    osal_printk("%s pair_complete conn_id:%d, status:%x\r\n", SLE_CLIENT_LOG, conn_id, status);
    if (status == 0) {
        ssap_exchange_info_t info = {0};
        info.mtu_size = SLE_MTU_SIZE;
        info.version = 1;
        ssapc_exchange_info_req(g_client_id, g_conn_id, &info);
        osal_printk("%s exchange info req sent\r\n", SLE_CLIENT_LOG);
    }
}

static void sle_client_exchange_info_cbk(uint8_t client_id, uint16_t conn_id,
                                         ssap_exchange_info_t *param, errcode_t status) {
    osal_printk("%s exchange_info mtu:%d, ver:%d, status:%x\r\n", SLE_CLIENT_LOG, param->mtu_size,
                param->version, status);

    ssapc_find_structure_param_t find_param = {0};
    find_param.type = SSAP_FIND_TYPE_PROPERTY;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xFFFF;
    ssapc_find_structure(client_id, conn_id, &find_param);
    osal_printk("%s start find structure...\r\n", SLE_CLIENT_LOG);
}

static void sle_client_find_structure_cbk(uint8_t client_id, uint16_t conn_id,
                                          ssapc_find_service_result_t *service, errcode_t status) {
    osal_printk("%s find_structure start_hdl:0x%02x, end_hdl:0x%02x, status:%x\r\n", SLE_CLIENT_LOG,
                service->start_hdl, service->end_hdl, status);
}

static void sle_client_find_property_cbk(uint8_t client_id, uint16_t conn_id,
                                         ssapc_find_property_result_t *property, errcode_t status) {
    osal_printk("%s find_property handle:%d, status:%x\r\n", SLE_CLIENT_LOG, property->handle,
                status);
    if (status == ERRCODE_SUCC) {
        g_property_handle = property->handle;
    }
}

static void sle_client_find_structure_cmp_cbk(uint8_t client_id, uint16_t conn_id,
                                              ssapc_find_structure_result_t *result,
                                              errcode_t status) {
    osal_printk("%s find_structure_cmp type:%d, status:%x\r\n", SLE_CLIENT_LOG, result->type,
                status);
    osal_printk("%s service discovery complete\r\n", SLE_CLIENT_LOG);

    if (g_property_handle != 0) {
        ssapc_read_req(client_id, g_conn_id, g_property_handle, SSAP_PROPERTY_TYPE_VALUE);
        osal_printk("%s read_req sent, handle=0x%04x\r\n", SLE_CLIENT_LOG, g_property_handle);
    }
}

static void sle_client_read_cfm_cbk(uint8_t client_id, uint16_t conn_id,
                                    ssapc_handle_value_t *read_data, errcode_t status) {
    osal_printk("%s read_cfm handle:0x%04x, len:%d, status:%x\r\n", SLE_CLIENT_LOG,
                read_data->handle, read_data->data_len, status);
    if (status == ERRCODE_SUCC && read_data->data_len > 0) {
        osal_printk("%s data:");
        for (uint16_t i = 0; i < read_data->data_len; i++) {
            osal_printk("%02x ", read_data->data[i]);
        }
        osal_printk("\r\n");
    }
}

static void sle_client_write_cfm_cbk(uint8_t client_id, uint16_t conn_id,
                                     ssapc_write_result_t *write_result, errcode_t status) {
    osal_printk("%s write_cfm handle:0x%04x, status:%x\r\n", SLE_CLIENT_LOG, write_result->handle,
                status);
}

static void sle_client_notification_cbk(uint8_t client_id, uint16_t conn_id,
                                        ssapc_handle_value_t *data, errcode_t status) {
    osal_printk("%s notification handle:0x%04x, len:%d\r\n", SLE_CLIENT_LOG, data->handle,
                data->data_len);
    if (data->data_len > 0) {
        osal_printk("%s data:");
        for (uint16_t i = 0; i < data->data_len; i++) {
            osal_printk("%02x ", data->data[i]);
        }
        osal_printk("\r\n");
    }
}

static void sle_client_indication_cbk(uint8_t client_id, uint16_t conn_id,
                                      ssapc_handle_value_t *data, errcode_t status) {
    osal_printk("%s indication handle:0x%04x, len:%d\r\n", SLE_CLIENT_LOG, data->handle,
                data->data_len);
}

static errcode_t sle_client_register_seek_cbks(void) {
    sle_announce_seek_callbacks_t cbk = {0};
    cbk.sle_enable_cb = sle_client_sle_enable_cbk;
    cbk.seek_enable_cb = sle_client_seek_enable_cbk;
    cbk.seek_result_cb = sle_client_seek_result_cbk;
    cbk.seek_disable_cb = sle_client_seek_disable_cbk;
    return sle_announce_seek_register_callbacks(&cbk);
}

static errcode_t sle_client_register_conn_cbks(void) {
    sle_connection_callbacks_t cbk = {0};
    cbk.connect_state_changed_cb = sle_client_connect_state_changed_cbk;
    cbk.pair_complete_cb = sle_client_pair_complete_cbk;
    return sle_connection_register_callbacks(&cbk);
}

static errcode_t sle_client_register_ssapc_cbks(void) {
    ssapc_callbacks_t cbk = {0};
    cbk.exchange_info_cb = sle_client_exchange_info_cbk;
    cbk.find_structure_cb = sle_client_find_structure_cbk;
    cbk.ssapc_find_property_cbk = sle_client_find_property_cbk;
    cbk.find_structure_cmp_cb = sle_client_find_structure_cmp_cbk;
    cbk.read_cfm_cb = sle_client_read_cfm_cbk;
    cbk.write_cfm_cb = sle_client_write_cfm_cbk;
    cbk.notification_cb = sle_client_notification_cbk;
    cbk.indication_cb = sle_client_indication_cbk;
    return ssapc_register_callbacks(&cbk);
}

errcode_t sle_client_init(void) {
    errcode_t ret = sle_client_register_seek_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s seek_cbks fail:%x\r\n", SLE_CLIENT_LOG, ret);
        return ret;
    }

    ret = sle_client_register_conn_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s conn_cbks fail:%x\r\n", SLE_CLIENT_LOG, ret);
        return ret;
    }

    ret = sle_client_register_ssapc_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssapc_cbks fail:%x\r\n", SLE_CLIENT_LOG, ret);
        return ret;
    }

    if (enable_sle() != ERRCODE_SUCC) {
        osal_printk("%s enable_sle fail!\r\n", SLE_CLIENT_LOG);
        return -1;
    }

    osal_printk("%s init ok\r\n", SLE_CLIENT_LOG);
    return ERRCODE_SLE_SUCCESS;
}
