#include "nv.h"
#include "securec.h"
#include "soc_osal.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_ssap_server.h"
#include "sle_decoy.h"

#define SLE_DECOY_LOG "[sle decoy]"
#define SLE_MTU_SIZE 520

/* Real controller service table captured by probe on H3863. UUID short values
 * are embedded in the last two bytes of a full 16-byte uuid (little-endian).
 * A len=2 uuid would make the find-rsp xx field 0000 — the wrong value. */
#define UUID_SVC0 0x0B06
#define UUID_SVC1 0x0906
#define UUID_P11 0x3C10
#define UUID_P12 0x3B10
#define UUID_P13 0x3910
#define UUID_P14 0x3A10
#define UUID_P16 0x3F10
#define UUID_P17 0x4010
#define UUID_P18 0x2E10

#define HID_MAP_LEN 69
#define MAX_ATTRS 8

/* Real controller service table captured by probe on H3863. UUID short values
 * are embedded in the last two bytes of a full 16-byte uuid (little-endian).
 * A len=2 uuid would make the find-rsp xx field 0000 — the wrong value. */
#define UUID_SVC0 0x0B06
#define UUID_SVC1 0x0906
#define UUID_P11 0x3C10
#define UUID_P12 0x3B10
#define UUID_P13 0x3910
#define UUID_P14 0x3A10
#define UUID_P16 0x3F10
#define UUID_P17 0x4010
#define UUID_P18 0x2E10

#define HID_MAP_LEN 69
#define MAX_ATTRS 8

static uint8_t g_server_id = 0;

/* Values captured from the real controller. */
static uint8_t g_val_11[4] = {0x00, 0x00, 0x00, 0x00};
static uint8_t g_cccd_11[2] = {0x02, 0x00};
static uint8_t g_val_12[8] = {0x01, 0x01, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t g_val_13[HID_MAP_LEN] = {0x00, 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1,
                                        0x00, 0x85, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15,
                                        0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95,
                                        0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09,
                                        0x31, 0x16, 0x01, 0xF8, 0x26, 0xFF, 0x07, 0x75, 0x0C, 0x95,
                                        0x02, 0x81, 0x06, 0x05, 0x01, 0x09, 0x38, 0x15, 0x81, 0x25,
                                        0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0};
static uint8_t g_val_14[2] = {0x06, 0x00};
static uint8_t g_val_16[] = "fly_digigs";
static uint8_t g_val_17[3] = {0x00, 0x05, 0x02};
static uint8_t g_val_18[] = "MAGIC-103F-12D1-0001";

/* handle -> storage mapping for read/write responses. */
typedef struct {
    uint16_t handle;
    uint8_t *buf;
    uint16_t len;
} attr_entry_t;

static attr_entry_t g_attrs[MAX_ATTRS];
static uint8_t g_attr_cnt = 0;

/* Flydigi base UUID: 37BE-A880-FC70-11EA-B720-00000000xxxx. */
static const uint8_t g_sle_base_uuid[] = {
    0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA, 0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void sle_set_uuid_u16(uint16_t u16, sle_uuid_t *out) {
    if (memcpy_s(out->uuid, SLE_UUID_LEN, g_sle_base_uuid, SLE_UUID_LEN) != EOK) {
        out->len = 0;
        return;
    }
    out->uuid[14] = u16 & 0xFF;
    out->uuid[15] = (u16 >> 8) & 0xFF;
    out->len = SLE_UUID_LEN;
}

static attr_entry_t *decoy_find_attr(uint16_t handle) {
    for (uint8_t i = 0; i < g_attr_cnt; i++) {
        if (g_attrs[i].handle == handle) {
            return &g_attrs[i];
        }
    }
    return NULL;
}

static void ssaps_mtu_changed_cb(
    uint8_t server_id, uint16_t conn_id, ssap_exchange_info_t *mtu_size, errcode_t status
) {
    osal_printk("[SSAP] mtu_changed mtu=%d status=%x\r\n", mtu_size->mtu_size, status);
}

static void ssaps_start_service_cb(uint8_t server_id, uint16_t handle, errcode_t status) {
    osal_printk("[SSAP] start_service handle=%x status=%x\r\n", handle, status);
}

static void
ssaps_add_service_cb(uint8_t server_id, sle_uuid_t *uuid, uint16_t handle, errcode_t status) {
    osal_printk("[SSAP] add_service handle=%x status=%x\r\n", handle, status);
}

static void ssaps_add_property_cb(
    uint8_t server_id, sle_uuid_t *uuid, uint16_t service_handle, uint16_t handle, errcode_t status
) {
    osal_printk(
        "[SSAP] add_property svc_hdl=%x prop_hdl=%x status=%x\r\n", service_handle, handle, status
    );
}

static void ssaps_add_descriptor_cb(
    uint8_t server_id, sle_uuid_t *uuid, uint16_t service_handle, uint16_t handle, errcode_t status
) {
    osal_printk(
        "[SSAP] add_descriptor svc_hdl=%x desc_hdl=%x status=%x\r\n", service_handle, handle, status
    );
}

static void sle_read_request_cb(
    uint8_t server_id, uint16_t conn_id, ssaps_req_read_cb_t *read_cb_para, errcode_t status
) {
    osal_printk(
        "[SSAP][RCV] read_req handle=0x%04x type=%d need_rsp=%d\r\n",
        read_cb_para->handle,
        read_cb_para->type,
        read_cb_para->need_rsp
    );

    attr_entry_t *attr = decoy_find_attr(read_cb_para->handle);
    if (read_cb_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = read_cb_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        if (attr != NULL) {
            rsp.value = attr->buf;
            rsp.value_len = attr->len;
            osal_printk("[SSAP][SND] read_rsp len=%d\r\n", attr->len);
        } else {
            osal_printk("[SSAP][SND] read_rsp empty (unknown hdl)\r\n");
        }
        ssaps_send_response(server_id, conn_id, &rsp);
    }
}

static void sle_write_request_cb(
    uint8_t server_id, uint16_t conn_id, ssaps_req_write_cb_t *write_cb_para, errcode_t status
) {
    osal_printk(
        "[SSAP][RCV] write_req handle=0x%04x type=%d len=%d need_rsp=%d data:",
        write_cb_para->handle,
        write_cb_para->type,
        write_cb_para->length,
        write_cb_para->need_rsp
    );
    for (uint16_t i = 0; i < write_cb_para->length; i++) {
        osal_printk("%02x ", write_cb_para->value[i]);
    }
    osal_printk("\r\n");

    if (write_cb_para->type == SSAP_DESCRIPTOR_CLIENT_CONFIGURATION && write_cb_para->length == 2 &&
        write_cb_para->value != NULL) {
        osal_printk(
            "[SSAP] *** CCCD write hdl=0x%04x value=%02x %02x\r\n",
            write_cb_para->handle,
            write_cb_para->value[0],
            write_cb_para->value[1]
        );
        memcpy_s(g_cccd_11, sizeof(g_cccd_11), write_cb_para->value, 2);
    } else {
        attr_entry_t *attr = decoy_find_attr(write_cb_para->handle);
        if (attr != NULL && write_cb_para->value != NULL) {
            uint16_t n = (write_cb_para->length <= attr->len) ? write_cb_para->length : attr->len;
            memcpy_s(attr->buf, attr->len, write_cb_para->value, n);
            osal_printk("[SSAP] stored %u bytes at hdl=0x%04x\r\n", n, write_cb_para->handle);
        }
    }

    if (write_cb_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = write_cb_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        ssaps_send_response(server_id, conn_id, &rsp);
        osal_printk("[SSAP][SND] write_rsp ok\r\n");
    }
}

static void sle_read_by_uuid_request_cb(
    uint8_t server_id, uint16_t conn_id, ssaps_req_read_by_uuid_cb_t *read_cb_para, errcode_t status
) {
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

static errcode_t decoy_add_property(
    uint16_t svc_handle,
    uint32_t oper,
    uint16_t perms,
    uint8_t *buf,
    uint16_t len,
    uint16_t uuid_val,
    uint16_t *hdl_out
) {
    ssaps_property_info_t property = {0};
    sle_set_uuid_u16(uuid_val, &property.uuid);
    property.permissions = perms;
    property.operate_indication = oper;
    property.value = buf;
    property.value_len = len;

    uint16_t handle = 0;
    errcode_t ret = ssaps_add_property_sync(g_server_id, svc_handle, &property, &handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("[SSAP] add_property fail:%x\r\n", ret);
        return ret;
    }
    if (g_attr_cnt < MAX_ATTRS && buf != NULL) {
        g_attrs[g_attr_cnt].handle = handle;
        g_attrs[g_attr_cnt].buf = buf;
        g_attrs[g_attr_cnt].len = len;
        g_attr_cnt++;
    }
    osal_printk(
        "[SSAP] property hdl=0x%x oper=0x%x len=%u uuid=0x%04x\r\n", handle, oper, len, uuid_val
    );
    if (hdl_out != NULL) {
        *hdl_out = handle;
    }
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t decoy_add_cccd(uint16_t svc_handle, uint16_t prop_handle) {
    ssaps_desc_info_t desc = {0};
    desc.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    desc.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    desc.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE;
    desc.value = g_cccd_11;
    desc.value_len = sizeof(g_cccd_11);
    errcode_t ret = ssaps_add_descriptor_sync(g_server_id, svc_handle, prop_handle, &desc);
    osal_printk("[SSAP] add cccd on 0x%x ret=%x\r\n", prop_handle, ret);
    return ret;
}

static errcode_t sle_decoy_add_services(void) {
    sle_uuid_t svc_uuid = {0};
    uint16_t h_svc0 = 0;
    uint16_t h_svc1 = 0;
    errcode_t ret;

    sle_set_uuid_u16(UUID_SVC0, &svc_uuid);
    ret = ssaps_add_service_sync(g_server_id, &svc_uuid, 1, &h_svc0);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("[SSAP] add_service0 fail:%x\r\n", ret);
        return ret;
    }
    osal_printk("[SSAP] svc0 handle=%x\r\n", h_svc0);

    uint16_t h_11 = 0;
    ret = decoy_add_property(
        h_svc0,
        0x30D,
        SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE,
        g_val_11,
        sizeof(g_val_11),
        UUID_P11,
        &h_11
    );
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    decoy_add_cccd(h_svc0, h_11);
    ret = decoy_add_property(
        h_svc0,
        0x5,
        SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE,
        g_val_12,
        sizeof(g_val_12),
        UUID_P12,
        NULL
    );
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    ret = decoy_add_property(
        h_svc0,
        SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE |
            SSAP_OPERATE_INDICATION_BIT_NOTIFY,
        SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE,
        g_val_13,
        HID_MAP_LEN,
        UUID_P13,
        NULL
    );
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    ret = decoy_add_property(
        h_svc0,
        SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP,
        SSAP_PERMISSION_WRITE,
        g_val_14,
        sizeof(g_val_14),
        UUID_P14,
        NULL
    );
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    ret = ssaps_start_service(g_server_id, h_svc0);
    osal_printk("[SSAP] start svc0 ret=%x\r\n", ret);

    sle_set_uuid_u16(UUID_SVC1, &svc_uuid);
    ret = ssaps_add_service_sync(g_server_id, &svc_uuid, 1, &h_svc1);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("[SSAP] add_service1 fail:%x\r\n", ret);
        return ret;
    }
    osal_printk("[SSAP] svc1 handle=%x\r\n", h_svc1);

    ret = decoy_add_property(
        h_svc1,
        SSAP_OPERATE_INDICATION_BIT_READ,
        SSAP_PERMISSION_READ,
        g_val_16,
        (uint16_t)(sizeof(g_val_16) - 1),
        UUID_P16,
        NULL
    );
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    ret = decoy_add_property(
        h_svc1,
        SSAP_OPERATE_INDICATION_BIT_READ,
        SSAP_PERMISSION_READ,
        g_val_17,
        sizeof(g_val_17),
        UUID_P17,
        NULL
    );
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    ret = decoy_add_property(
        h_svc1,
        SSAP_OPERATE_INDICATION_BIT_READ,
        SSAP_PERMISSION_READ,
        g_val_18,
        (uint16_t)(sizeof(g_val_18) - 1),
        UUID_P18,
        NULL
    );
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    ret = ssaps_start_service(g_server_id, h_svc1);
    osal_printk("[SSAP] start svc1 ret=%x\r\n", ret);

    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_decoy_start(void) {
    /* Mirror the BS21 decoy: advertise SSAP info before registering the
     * server/services so an incoming exchange request gets the right framing. */
    ssap_exchange_info_t info = {0};
    info.mtu_size = SLE_MTU_SIZE;
    info.version = 0;
    errcode_t ret = ssaps_set_info(0, &info);
    osal_printk("[SSAP] set_info mtu=%u ver=%u ret=%x\r\n", info.mtu_size, info.version, ret);

    sle_uuid_t app_uuid = {0};
    app_uuid.len = 2;
    app_uuid.uuid[0] = 0x34;
    app_uuid.uuid[1] = 0x12;
    ssaps_register_server(&app_uuid, &g_server_id);

    ret = sle_decoy_add_services();
    osal_printk("%s add_services ret=%x\r\n", SLE_DECOY_LOG, ret);
    if (ret != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ret;
    }

    osal_printk("[SSAP] server started server_id=%x\r\n", g_server_id);

    osal_printk("%s calling adv_init\r\n", SLE_DECOY_LOG);
    ret = sle_decoy_adv_init();
    osal_printk("%s adv_init ret=%x\r\n", SLE_DECOY_LOG, ret);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s adv_init fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    return ERRCODE_SLE_SUCCESS;
}

static void sle_set_local_addr_spoof(void) {
    sle_addr_t la = {0};
    la.type = SLE_ADDRESS_TYPE_PUBLIC;
    la.addr[0] = 0xA1;
    la.addr[1] = 0xA2;
    la.addr[2] = 0xC8;
    la.addr[3] = 0x75;
    la.addr[4] = 0x43;
    la.addr[5] = 0xB8;
    errcode_t ret_set = sle_set_local_addr(&la);
    if (ret_set != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s set_local_addr fail:%x\r\n", SLE_DECOY_LOG, ret_set);
    }
}

static void sle_enable_cb(errcode_t status) {
    osal_printk("[SLE] enable_cb status=%x\r\n", status);
    if (status == ERRCODE_SLE_SUCCESS) {
        sle_set_local_addr_spoof();
        sle_decoy_start();
    }
}

static void sle_connect_state_changed_cb(
    uint16_t conn_id,
    const sle_addr_t *addr,
    sle_acb_state_t conn_state,
    sle_pair_state_t pair_state,
    sle_disc_reason_t disc_reason
) {
    osal_printk(
        "[CONN] state conn_id:0x%02x state:0x%x pair:0x%x disc:0x%x\r\n",
        conn_id,
        conn_state,
        pair_state,
        disc_reason
    );
}

static void sle_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status) {
    osal_printk("[PAIR] complete conn_id:0x%02x status:%x\r\n", conn_id, status);

    ssap_exchange_info_t info = {0};
    info.mtu_size = SLE_MTU_SIZE;
    info.version = 1;
    ssaps_set_info(g_server_id, &info);
}

static void sle_auth_complete_cb(
    uint16_t conn_id, const sle_addr_t *addr, errcode_t status, const sle_auth_info_evt_t *evt
) {
    osal_printk(
        "[AUTH] complete conn_id:0x%02x status:%x bond:%d\r\n",
        conn_id,
        status,
        (evt != NULL) ? evt->is_bond : -1
    );
}

static void sle_connect_param_update_req_cb(
    uint16_t conn_id, errcode_t status, const sle_connection_param_update_req_t *param
) {
    osal_printk(
        "[CONN] param_update_req conn=%u status=%x min=%u max=%u lat=%u to=%u\r\n",
        conn_id,
        status,
        param->interval_min,
        param->interval_max,
        param->max_latency,
        param->supervision_timeout
    );
}

static errcode_t sle_decoy_register_ssaps_cbks(void) {
    ssaps_callbacks_t cbk = {0};
    cbk.add_service_cb = ssaps_add_service_cb;
    cbk.add_property_cb = ssaps_add_property_cb;
    cbk.add_descriptor_cb = ssaps_add_descriptor_cb;
    cbk.start_service_cb = ssaps_start_service_cb;
    cbk.mtu_changed_cb = ssaps_mtu_changed_cb;
    cbk.read_request_cb = sle_read_request_cb;
    cbk.write_request_cb = sle_write_request_cb;
    cbk.read_by_uuid_request_cb = sle_read_by_uuid_request_cb;
    return ssaps_register_callbacks(&cbk);
}

static errcode_t sle_decoy_register_conn_cbks(void) {
    sle_connection_callbacks_t cbk = {0};
    cbk.connect_state_changed_cb = sle_connect_state_changed_cb;
    cbk.connect_param_update_req_cb = sle_connect_param_update_req_cb;
    cbk.pair_complete_cb = sle_pair_complete_cb;
    cbk.auth_complete_cb = sle_auth_complete_cb;
    return sle_connection_register_callbacks(&cbk);
}

static errcode_t sle_decoy_register_adv_cbks(void) {
    sle_announce_seek_callbacks_t cbk = {0};
    cbk.sle_enable_cb = sle_enable_cb;
    return sle_announce_seek_register_callbacks(&cbk);
}

errcode_t sle_decoy_init(void) {
    errcode_t ret;

    /* Mirror the BS21 decoy: NV must be ready before pairing keys can be
     * stored, otherwise the dongle's encrypted link fails after MTU exchange. */
    uapi_nv_init();

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

    ret = enable_sle();
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s enable_sle fail:%x\r\n", SLE_DECOY_LOG, ret);
        return -1;
    }

    osal_printk("%s init ok\r\n", SLE_DECOY_LOG);
    return ERRCODE_SLE_SUCCESS;
}