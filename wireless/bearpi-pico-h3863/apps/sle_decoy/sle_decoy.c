#include "securec.h"
#include "soc_osal.h"
#include "sle_errcode.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_ssap_server.h"
#include "sle_decoy.h"

#define SLE_DECOY_LOG "[sle decoy]"
#define SLE_MTU_SIZE 520

#define SLE_SERVICE_UUID 0x1234
#define SLE_PROPERTY_UUID 0x5678

static uint8_t g_server_id = 0;
static uint16_t g_service_handle = 0;
static uint16_t g_property_handle = 0;
static uint16_t g_conn_hdl = 0;

static uint8_t g_sle_base_uuid[] = {0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
                                     0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static void encode2byte_little(uint8_t *ptr, uint16_t data) {
    *(uint8_t *)(ptr + 1) = (uint8_t)(data >> 0x8);
    *(uint8_t *)ptr = (uint8_t)data;
}

static void sle_set_uuid_base(sle_uuid_t *out) {
    if (memcpy_s(out->uuid, SLE_UUID_LEN, g_sle_base_uuid, SLE_UUID_LEN) != EOK) {
        out->len = 0;
        return;
    }
    out->len = 2;
}

static void sle_set_uuid_u2(uint16_t u2, sle_uuid_t *out) {
    sle_set_uuid_base(out);
    out->len = 2;
    encode2byte_little(&out->uuid[14], u2);
}

static void ssaps_mtu_changed_cb(uint8_t server_id, uint16_t conn_id,
                                  ssap_exchange_info_t *mtu_size, errcode_t status) {
    osal_printk("[SSAP] mtu_changed mtu=%d status=%x\r\n", mtu_size->mtu_size, status);
}

static void ssaps_start_service_cb(uint8_t server_id, uint16_t handle, errcode_t status) {
    osal_printk("[SSAP] start_service handle=%x status=%x\r\n", handle, status);
}

static void ssaps_add_service_cb(uint8_t server_id, sle_uuid_t *uuid, uint16_t handle,
                                  errcode_t status) {
    osal_printk("[SSAP] add_service handle=%x status=%x\r\n", handle, status);
}

static void ssaps_add_property_cb(uint8_t server_id, sle_uuid_t *uuid, uint16_t service_handle,
                                   uint16_t handle, errcode_t status) {
    osal_printk("[SSAP] add_property svc_hdl=%x prop_hdl=%x status=%x\r\n",
                service_handle, handle, status);
}

static void sle_read_request_cb(uint8_t server_id, uint16_t conn_id,
                                 ssaps_req_read_cb_t *read_cb_para, errcode_t status) {
    osal_printk("[SSAP][RCV] read_req handle=0x%04x type=%d need_rsp=%d\r\n",
                read_cb_para->handle, read_cb_para->type, read_cb_para->need_rsp);

    if (read_cb_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = read_cb_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        rsp.value = NULL;
        rsp.value_len = 0;
        ssaps_send_response(server_id, conn_id, &rsp);
        osal_printk("[SSAP][SND] read_rsp empty\r\n");
    }
}

static void sle_write_request_cb(uint8_t server_id, uint16_t conn_id,
                                  ssaps_req_write_cb_t *write_cb_para, errcode_t status) {
    osal_printk("[SSAP][RCV] write_req handle=0x%04x len=%d need_rsp=%d data:",
                write_cb_para->handle, write_cb_para->length, write_cb_para->need_rsp);
    for (uint16_t i = 0; i < write_cb_para->length; i++) {
        osal_printk("%02x ", write_cb_para->value[i]);
    }
    osal_printk("\r\n");

    if (write_cb_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = write_cb_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        ssaps_send_response(server_id, conn_id, &rsp);
        osal_printk("[SSAP][SND] write_rsp ok\r\n");
    }
}

static void sle_read_by_uuid_request_cb(uint8_t server_id, uint16_t conn_id,
                                         ssaps_req_read_by_uuid_cb_t *read_cb_para,
                                         errcode_t status) {
    osal_printk("[SSAP][RCV] read_by_uuid_req uuid=");
    for (uint8_t i = 0; i < read_cb_para->uuid.len; i++) {
        osal_printk("%02x", read_cb_para->uuid.uuid[i]);
    }
    osal_printk(" need_rsp=%d\r\n", read_cb_para->need_rsp);

    if (read_cb_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = read_cb_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        rsp.value = NULL;
        rsp.value_len = 0;
        ssaps_send_response(server_id, conn_id, &rsp);
        osal_printk("[SSAP][SND] read_by_uuid_rsp empty\r\n");
    }
}

static errcode_t sle_decoy_register_ssaps_cbks(void) {
    ssaps_callbacks_t cbk = {0};
    cbk.add_service_cb = ssaps_add_service_cb;
    cbk.add_property_cb = ssaps_add_property_cb;
    cbk.start_service_cb = ssaps_start_service_cb;
    cbk.mtu_changed_cb = ssaps_mtu_changed_cb;
    cbk.read_request_cb = sle_read_request_cb;
    cbk.write_request_cb = sle_write_request_cb;
    cbk.read_by_uuid_request_cb = sle_read_by_uuid_request_cb;
    return ssaps_register_callbacks(&cbk);
}

static errcode_t sle_decoy_add_service(void) {
    sle_uuid_t service_uuid = {0};
    sle_set_uuid_u2(SLE_SERVICE_UUID, &service_uuid);
    uint16_t ret = ssaps_add_service_sync(g_server_id, &service_uuid, 1, &g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("[SSAP] add_service fail:%x\r\n", ret);
        return ret;
    }
    osal_printk("[SSAP] service added handle=%x\r\n", g_service_handle);
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_decoy_add_property(void) {
    ssaps_property_info_t property = {0};
    uint8_t value[] = {0x01, 0x02, 0x03, 0x04};

    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    property.operate_indication =
        SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE;
    sle_set_uuid_u2(SLE_PROPERTY_UUID, &property.uuid);
    property.value = value;
    property.value_len = sizeof(value);

    uint16_t ret = ssaps_add_property_sync(g_server_id, g_service_handle, &property,
                                            &g_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("[SSAP] add_property fail:%x\r\n", ret);
        return ret;
    }
    osal_printk("[SSAP] property added handle=%x\r\n", g_property_handle);
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_decoy_start(void) {
    sle_uuid_t app_uuid = {0};
    uint8_t app_uuid_data[2] = {0x12, 0x34};
    app_uuid.len = 2;
    if (memcpy_s(app_uuid.uuid, 2, app_uuid_data, 2) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    ssaps_register_server(&app_uuid, &g_server_id);

    errcode_t ret = sle_decoy_add_service();
    if (ret != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ret;
    }

    ret = sle_decoy_add_property();
    if (ret != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ret;
    }

    ret = ssaps_start_service(g_server_id, g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("[SSAP] start_service fail:%x\r\n", ret);
        return ret;
    }

    osal_printk("[SSAP] server started server_id=%x\r\n", g_server_id);

    ret = sle_decoy_adv_init();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s adv_init fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    return ERRCODE_SLE_SUCCESS;
}

static void sle_enable_cb(errcode_t status) {
    osal_printk("[SLE] enable_cb status=%x\r\n", status);
    if (status == ERRCODE_SLE_SUCCESS) {
        sle_decoy_start();
    }
}

static void sle_connect_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
                                          sle_acb_state_t conn_state,
                                          sle_pair_state_t pair_state,
                                          sle_disc_reason_t disc_reason) {
    osal_printk("[CONN] state conn_id:0x%02x state:0x%x pair:0x%x disc:0x%x\r\n",
                conn_id, conn_state, pair_state, disc_reason);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_conn_hdl = conn_id;
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        g_conn_hdl = 0;
    }
}

static void sle_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status) {
    osal_printk("[PAIR] complete conn_id:0x%02x status:%x\r\n", conn_id, status);

    ssap_exchange_info_t info = {0};
    info.mtu_size = SLE_MTU_SIZE;
    info.version = 1;
    ssaps_set_info(g_server_id, &info);
}

static errcode_t sle_decoy_register_conn_cbks(void) {
    sle_connection_callbacks_t cbk = {0};
    cbk.connect_state_changed_cb = sle_connect_state_changed_cb;
    cbk.pair_complete_cb = sle_pair_complete_cb;
    return sle_connection_register_callbacks(&cbk);
}

static errcode_t sle_decoy_register_adv_cbks(void) {
    sle_announce_seek_callbacks_t cbk = {0};
    cbk.sle_enable_cb = sle_enable_cb;
    return sle_announce_seek_register_callbacks(&cbk);
}

errcode_t sle_decoy_init(void) {
    errcode_t ret;

    ret = sle_decoy_register_ssaps_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_cbks fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    ret = sle_decoy_register_conn_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s conn_cbks fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    ret = sle_decoy_register_adv_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s adv_cbks fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    if (enable_sle() != ERRCODE_SUCC) {
        osal_printk("%s enable_sle fail!\r\n", SLE_DECOY_LOG);
        return -1;
    }

    osal_printk("%s init ok\r\n", SLE_DECOY_LOG);
    return ERRCODE_SLE_SUCCESS;
}
