#include "securec.h"
#include "soc_osal.h"
#include "sle_errcode.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_ssap_server.h"
#include "sle_server.h"

#define SLE_MTU_SIZE 520
#define OCTET_BIT_LEN 8
#define UUID_LEN_2 2
#define UUID_INDEX 14
#define BT_INDEX_4 4
#define BT_INDEX_0 0

#define SLE_SERVICE_UUID 0x1234
#define SLE_PROPERTY_UUID 0x5678

#define SLE_SERVER_LOG "[sle server]"

static uint8_t g_server_id = 0;
static uint16_t g_service_handle = 0;
static uint16_t g_property_handle = 0;
static uint16_t g_sle_conn_hdl = 0;

static char g_property_value[OCTET_BIT_LEN] = {0};

static uint8_t g_sle_base_uuid[] = {
    0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA, 0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void encode2byte_little(uint8_t *ptr, uint16_t data) {
    *(uint8_t *)(ptr + 1) = (uint8_t)(data >> 0x8);
    *(uint8_t *)ptr = (uint8_t)data;
}

static void sle_set_uuid_base(sle_uuid_t *out) {
    errcode_t ret;
    ret = memcpy_s(out->uuid, SLE_UUID_LEN, g_sle_base_uuid, SLE_UUID_LEN);
    if (ret != EOK) {
        out->len = 0;
        return;
    }
    out->len = UUID_LEN_2;
}

static void sle_set_uuid_u2(uint16_t u2, sle_uuid_t *out) {
    sle_set_uuid_base(out);
    out->len = UUID_LEN_2;
    encode2byte_little(&out->uuid[UUID_INDEX], u2);
}

static void ssaps_mtu_changed_cbk(
    uint8_t server_id, uint16_t conn_id, ssap_exchange_info_t *mtu_size, errcode_t status
) {
    osal_printk(
        "%s mtu_changed server_id:%x, conn_id:%x, mtu:%x, status:%x\r\n",
        SLE_SERVER_LOG,
        server_id,
        conn_id,
        mtu_size->mtu_size,
        status
    );
}

static void ssaps_start_service_cbk(uint8_t server_id, uint16_t handle, errcode_t status) {
    osal_printk(
        "%s start_service server_id:%d, handle:%x, status:%x\r\n",
        SLE_SERVER_LOG,
        server_id,
        handle,
        status
    );
}

static void ssaps_add_service_cbk(
    uint8_t server_id, sle_uuid_t *uuid, uint16_t handle, errcode_t status
) {
    osal_printk(
        "%s add_service server_id:%x, handle:%x, status:%x\r\n",
        SLE_SERVER_LOG,
        server_id,
        handle,
        status
    );
}

static void ssaps_add_property_cbk(
    uint8_t server_id, sle_uuid_t *uuid, uint16_t service_handle, uint16_t handle, errcode_t status
) {
    osal_printk(
        "%s add_property server_id:%x, svc_hdl:%x, prop_hdl:%x, status:%x\r\n",
        SLE_SERVER_LOG,
        server_id,
        service_handle,
        handle,
        status
    );
}

static void ssaps_add_descriptor_cbk(
    uint8_t server_id,
    sle_uuid_t *uuid,
    uint16_t service_handle,
    uint16_t property_handle,
    errcode_t status
) {
    osal_printk(
        "%s add_descriptor server_id:%x, svc_hdl:%x, prop_hdl:%x, status:%x\r\n",
        SLE_SERVER_LOG,
        server_id,
        service_handle,
        property_handle,
        status
    );
}

static void sle_read_request_cb(
    uint8_t server_id, uint16_t conn_id, ssaps_req_read_cb_t *read_cb_para, errcode_t status
) {
    osal_printk(
        "%s read_req handle=0x%04x, type=0x%x\r\n",
        SLE_SERVER_LOG,
        read_cb_para->handle,
        read_cb_para->type
    );

    if (read_cb_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = read_cb_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        rsp.value = (uint8_t *)g_property_value;
        rsp.value_len = sizeof(g_property_value);
        ssaps_send_response(server_id, conn_id, &rsp);
        osal_printk("%s read_rsp sent\r\n", SLE_SERVER_LOG);
    }
}

static void sle_write_request_cb(
    uint8_t server_id, uint16_t conn_id, ssaps_req_write_cb_t *write_cb_para, errcode_t status
) {
    osal_printk(
        "%s write_req handle=0x%04x, len=%d\r\n",
        SLE_SERVER_LOG,
        write_cb_para->handle,
        write_cb_para->length
    );

    if (write_cb_para->length > sizeof(g_property_value)) {
        osal_printk("%s write data too large, rejected\r\n", SLE_SERVER_LOG);
        return;
    }

    if (memcpy_s(
            g_property_value, sizeof(g_property_value), write_cb_para->value, write_cb_para->length
        ) != EOK) {
        osal_printk("%s write memcpy failed\r\n", SLE_SERVER_LOG);
        return;
    }

    osal_printk("%s property updated, data:");
    for (uint16_t i = 0; i < write_cb_para->length; i++) {
        osal_printk("%02x ", write_cb_para->value[i]);
    }
    osal_printk("\r\n");

    if (write_cb_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = write_cb_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        ssaps_send_response(server_id, conn_id, &rsp);
        osal_printk("%s write_rsp sent\r\n", SLE_SERVER_LOG);
    }
}

static void ssaps_delete_all_service_cbk(uint8_t server_id, errcode_t status) {
    osal_printk(
        "%s delete_all_service server_id:%x, status:%x\r\n", SLE_SERVER_LOG, server_id, status
    );
}

static errcode_t sle_server_ssaps_register_cbks(void) {
    ssaps_callbacks_t cbk = {0};
    cbk.add_service_cb = ssaps_add_service_cbk;
    cbk.add_property_cb = ssaps_add_property_cbk;
    cbk.add_descriptor_cb = ssaps_add_descriptor_cbk;
    cbk.start_service_cb = ssaps_start_service_cbk;
    cbk.delete_all_service_cb = ssaps_delete_all_service_cbk;
    cbk.mtu_changed_cb = ssaps_mtu_changed_cbk;
    cbk.read_request_cb = sle_read_request_cb;
    cbk.write_request_cb = sle_write_request_cb;
    return ssaps_register_callbacks(&cbk);
}

static errcode_t sle_server_add_service(void) {
    errcode_t ret;
    sle_uuid_t service_uuid = {0};
    sle_set_uuid_u2(SLE_SERVICE_UUID, &service_uuid);
    ret = ssaps_add_service_sync(g_server_id, &service_uuid, 1, &g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add_service fail, ret:%x\r\n", SLE_SERVER_LOG, ret);
        return ret;
    }
    osal_printk("%s service added, handle:%x\r\n", SLE_SERVER_LOG, g_service_handle);
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_server_add_property(void) {
    errcode_t ret;
    ssaps_property_info_t property = {0};
    ssaps_desc_info_t descriptor = {0};
    uint8_t ntf_value[] = {0x01, 0x0};

    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    property.operate_indication =
        SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE;
    sle_set_uuid_u2(SLE_PROPERTY_UUID, &property.uuid);
    property.value = (uint8_t *)osal_vmalloc(sizeof(g_property_value));
    if (property.value == NULL) {
        return ERRCODE_SLE_FAIL;
    }
    if (memcpy_s(
            property.value, sizeof(g_property_value), g_property_value, sizeof(g_property_value)
        ) != EOK) {
        osal_vfree(property.value);
        return ERRCODE_SLE_FAIL;
    }
    ret = ssaps_add_property_sync(g_server_id, g_service_handle, &property, &g_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add_property fail, ret:%x\r\n", SLE_SERVER_LOG, ret);
        osal_vfree(property.value);
        return ret;
    }
    osal_printk("%s property added, handle:%x\r\n", SLE_SERVER_LOG, g_property_handle);

    descriptor.permissions = SSAP_PERMISSION_READ;
    descriptor.type = SSAP_DESCRIPTOR_USER_DESCRIPTION;
    descriptor.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ;
    descriptor.value = ntf_value;
    descriptor.value_len = sizeof(ntf_value);
    ret = ssaps_add_descriptor_sync(g_server_id, g_service_handle, g_property_handle, &descriptor);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add_descriptor fail, ret:%x\r\n", SLE_SERVER_LOG, ret);
        osal_vfree(property.value);
        return ret;
    }

    osal_vfree(property.value);
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_server_start(void) {
    errcode_t ret;
    sle_uuid_t app_uuid = {0};
    uint8_t app_uuid_data[UUID_LEN_2] = {0x12, 0x34};

    app_uuid.len = sizeof(app_uuid_data);
    if (memcpy_s(app_uuid.uuid, app_uuid.len, app_uuid_data, sizeof(app_uuid_data)) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    ssaps_register_server(&app_uuid, &g_server_id);

    ret = sle_server_add_service();
    if (ret != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ret;
    }

    ret = sle_server_add_property();
    if (ret != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ret;
    }

    ret = ssaps_start_service(g_server_id, g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s start_service fail, ret:%x\r\n", SLE_SERVER_LOG, ret);
        return ret;
    }

    osal_printk("%s server started, server_id:%x\r\n", SLE_SERVER_LOG, g_server_id);
    return ERRCODE_SLE_SUCCESS;
}

static void sle_connect_state_changed_cbk(
    uint16_t conn_id,
    const sle_addr_t *addr,
    sle_acb_state_t conn_state,
    sle_pair_state_t pair_state,
    sle_disc_reason_t disc_reason
) {
    osal_printk(
        "%s conn_state conn_id:0x%02x, state:0x%x, pair:0x%x, disc:0x%x\r\n",
        SLE_SERVER_LOG,
        conn_id,
        conn_state,
        pair_state,
        disc_reason
    );
    osal_printk(
        "%s addr:%02x:**:**:**:%02x:%02x\r\n",
        SLE_SERVER_LOG,
        addr->addr[BT_INDEX_0],
        addr->addr[BT_INDEX_4]
    );

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_sle_conn_hdl = conn_id;
        osal_printk("%s CONNECTED, conn_id=0x%02x\r\n", SLE_SERVER_LOG, conn_id);
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        osal_printk("%s DISCONNECTED, re-start announce\r\n", SLE_SERVER_LOG);
        g_sle_conn_hdl = 0;
        sle_start_announce(SLE_ADV_HANDLE_DEFAULT);
    }
}

static void sle_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status) {
    osal_printk("%s pair_complete conn_id:%d, status:%x\r\n", SLE_SERVER_LOG, conn_id, status);

    ssap_exchange_info_t info = {0};
    info.mtu_size = SLE_MTU_SIZE;
    info.version = 1;
    ssaps_set_info(g_server_id, &info);

    uint8_t hello[] = "Hello from flydigi-m2 server!";
    ssaps_ntf_ind_t ntf = {0};
    ntf.handle = g_property_handle;
    ntf.type = SSAP_PROPERTY_TYPE_VALUE;
    ntf.value = hello;
    ntf.value_len = sizeof(hello) - 1;
    ssaps_notify_indicate(g_server_id, g_sle_conn_hdl, &ntf);
    osal_printk("%s sent hello notification\r\n", SLE_SERVER_LOG);
}

static errcode_t sle_server_conn_register_cbks(void) {
    sle_connection_callbacks_t cbk = {0};
    cbk.connect_state_changed_cb = sle_connect_state_changed_cbk;
    cbk.pair_complete_cb = sle_pair_complete_cbk;
    return sle_connection_register_callbacks(&cbk);
}

errcode_t sle_server_init(void) {
    errcode_t ret;

    if (enable_sle() != ERRCODE_SUCC) {
        osal_printk("%s enable_sle fail!\r\n", SLE_SERVER_LOG);
        return -1;
    }

    ret = sle_server_announce_register_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s announce_cbks fail:%x\r\n", SLE_SERVER_LOG, ret);
        return ret;
    }

    ret = sle_server_conn_register_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s conn_cbks fail:%x\r\n", SLE_SERVER_LOG, ret);
        return ret;
    }

    ret = sle_server_ssaps_register_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps_cbks fail:%x\r\n", SLE_SERVER_LOG, ret);
        return ret;
    }

    ret = sle_server_start();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s server_start fail:%x\r\n", SLE_SERVER_LOG, ret);
        return ret;
    }

    ret = sle_server_adv_init();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s adv_init fail:%x\r\n", SLE_SERVER_LOG, ret);
        return ret;
    }

    osal_printk("%s init ok\r\n", SLE_SERVER_LOG);
    return ERRCODE_SLE_SUCCESS;
}
