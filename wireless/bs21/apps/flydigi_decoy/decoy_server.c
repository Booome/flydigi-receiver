#include "decoy_server.h"
#include "securec.h"
#include "sle_common.h"
#include "sle_errcode.h"
#include "sle_ssap_server.h"
#include "soc_osal.h"

#define DECOY_LOG "[decoy]"

/* *****************************************************************************
 * Attribute storage — mirrors the real controller layout (experiment N)
 * *****************************************************************************/

#define VAL11_LEN 8
#define VAL12_LEN 8
#define MAP13_LEN 69
#define VAL14_LEN 2

static uint8_t g_val_11[VAL11_LEN];
static uint8_t g_cccd_11[2];
static uint8_t g_val_12[VAL12_LEN];
static uint8_t g_val_14[VAL14_LEN];

/* Report map captured from the real controller. First 51 bytes known from
 * the read log, remainder zero-padded to the declared length of 69. */
static uint8_t g_map_13[MAP13_LEN] = {
    0x00, 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x85, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75,
    0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30,
    0x09, 0x31, 0x16, 0x01, 0xF8, 0x26, 0xFF, 0x07, 0x75, 0x0C, 0x95, 0x02,
};

static uint8_t g_val_16[] = "fly_digigs";
static uint8_t g_val_17[3] = {0x00, 0x05, 0x02};
static uint8_t g_val_18[] = "MAGIC-103F-12D1-0001";

/* handle -> storage mapping filled in during registration so that reads
 * return what was written (the controller behaves as a raw data pipe). */
typedef struct {
    uint16_t handle;
    uint8_t *buf;
    uint16_t len;
} attr_entry_t;

#define MAX_ATTRS 8
static attr_entry_t g_attrs[MAX_ATTRS];
static uint8_t g_attr_cnt = 0;

static uint8_t g_server_id = 0;

/* *****************************************************************************
 * Helpers
 * *****************************************************************************/

static void decoy_print_hex(const char *tag, const uint8_t *buf, uint16_t len) {
    osal_printk("%s %s ", DECOY_LOG, tag);
    for (uint16_t i = 0; i < len; i++) {
        osal_printk("%02x ", buf[i]);
    }
    osal_printk("\r\n");
}

static attr_entry_t *decoy_find_attr(uint16_t handle) {
    for (uint8_t i = 0; i < g_attr_cnt; i++) {
        if (g_attrs[i].handle == handle) {
            return &g_attrs[i];
        }
    }
    return NULL;
}

static errcode_t decoy_add_uuid2(sle_uuid_t *uuid) {
    uuid->len = 2;
    uuid->uuid[0] = 0x37;
    uuid->uuid[1] = 0xBE;
    return ERRCODE_SUCC;
}

/* Register one property and record its handle in the lookup table. */
static errcode_t decoy_add_property(uint16_t svc_hdl, uint32_t oper, uint16_t perms, uint8_t *buf,
                                    uint16_t len, uint16_t *hdl_out) {
    ssaps_property_info_t prop = {0};
    errcode_t ret = decoy_add_uuid2(&prop.uuid);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    prop.permissions = perms;
    prop.operate_indication = oper;
    prop.value_len = len;
    prop.value = buf;

    uint16_t hdl = 0;
    ret = ssaps_add_property_sync(g_server_id, svc_hdl, &prop, &hdl);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s add_property fail 0x%x\r\n", DECOY_LOG, ret);
        return ret;
    }
    if (g_attr_cnt < MAX_ATTRS && buf != NULL) {
        g_attrs[g_attr_cnt].handle = hdl;
        g_attrs[g_attr_cnt].buf = buf;
        g_attrs[g_attr_cnt].len = len;
        g_attr_cnt++;
    }
    osal_printk("%s   property @0x%02x oper=0x%x len=%u\r\n", DECOY_LOG, hdl, oper, len);
    if (hdl_out != NULL) {
        *hdl_out = hdl;
    }
    return ERRCODE_SUCC;
}

/* *****************************************************************************
 * SSAP callbacks — full behavior logging
 * *****************************************************************************/

static void decoy_mtu_changed_cb(uint8_t server_id, uint16_t conn_id, ssap_exchange_info_t *info,
                                 errcode_t status) {
    osal_printk("%s mtu changed: sid=%u conn=%u mtu=%u status=0x%x\r\n", DECOY_LOG, server_id,
                conn_id, info->mtu_size, status);
}

static void decoy_read_cb(uint8_t server_id, uint16_t conn_id, ssaps_req_read_cb_t *read_para,
                          errcode_t status) {
    osal_printk("%s READ conn=%u hdl=0x%x type=0x%02x\r\n", DECOY_LOG, conn_id, read_para->handle,
                read_para->type);
    attr_entry_t *attr = decoy_find_attr(read_para->handle);
    if (attr == NULL) {
        osal_printk("%s READ unknown handle\r\n", DECOY_LOG);
        return;
    }
    decoy_print_hex("READ value:", attr->buf, attr->len);

    ssaps_send_rsp_t rsp = {0};
    rsp.request_id = read_para->request_id;
    rsp.status = ERRCODE_SLE_SUCCESS;
    rsp.value_len = attr->len;
    rsp.value = attr->buf;
    errcode_t ret = ssaps_send_response(server_id, conn_id, &rsp);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s send_response fail 0x%x\r\n", DECOY_LOG, ret);
    }
}

static void decoy_write_cb(uint8_t server_id, uint16_t conn_id, ssaps_req_write_cb_t *write_para,
                           errcode_t status) {
    /* CCCD writes carry the descriptor type; log them prominently. */
    if (write_para->type == SSAP_DESCRIPTOR_CLIENT_CONFIGURATION && write_para->length == 2 &&
        write_para->value != NULL) {
        osal_printk("%s *** CCC WRITE conn=%u hdl=0x%x value=%02x %02x\r\n", DECOY_LOG, conn_id,
                    write_para->handle, write_para->value[0], write_para->value[1]);
        if (memcpy_s(g_cccd_11, sizeof(g_cccd_11), write_para->value, 2) != EOK) {
            osal_printk("%s cccd store fail\r\n", DECOY_LOG);
        }
        return;
    }

    osal_printk("%s WRITE conn=%u hdl=0x%x type=0x%02x need_rsp=%d len=%u\r\n", DECOY_LOG, conn_id,
                write_para->handle, write_para->type, write_para->need_rsp, write_para->length);
    if (write_para->length > 0 && write_para->value != NULL) {
        decoy_print_hex("WRITE payload:", write_para->value, write_para->length);
    }

    attr_entry_t *attr = decoy_find_attr(write_para->handle);
    if (attr != NULL) {
        uint16_t n = (write_para->length <= attr->len) ? write_para->length : attr->len;
        if (memcpy_s(attr->buf, attr->len, write_para->value, n) == EOK) {
            osal_printk("%s stored %u bytes at hdl=0x%x\r\n", DECOY_LOG, n, write_para->handle);
        }
    } else {
        osal_printk("%s WRITE to untracked hdl\r\n", DECOY_LOG);
    }

    if (write_para->need_rsp) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = write_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        errcode_t ret = ssaps_send_response(server_id, conn_id, &rsp);
        if (ret != ERRCODE_SUCC) {
            osal_printk("%s write send_response fail 0x%x\r\n", DECOY_LOG, ret);
        }
    }
}

/* *****************************************************************************
 * Service registration — mirrors the controller attribute table
 * *****************************************************************************/

void decoy_server_init(void) {
    sle_uuid_t app_uuid = {0};
    app_uuid.len = 16;
    errcode_t ret = ssaps_register_server(&app_uuid, &g_server_id);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s register_server fail 0x%x\r\n", DECOY_LOG, ret);
        return;
    }

    ssaps_callbacks_t cbks = {0};
    cbks.read_request_cb = decoy_read_cb;
    cbks.write_request_cb = decoy_write_cb;
    cbks.mtu_changed_cb = decoy_mtu_changed_cb;
    ret = ssaps_register_callbacks(&cbks);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s register_callbacks fail 0x%x\r\n", DECOY_LOG, ret);
        return;
    }

    sle_uuid_t svc_uuid = {0};
    ret = decoy_add_uuid2(&svc_uuid);
    if (ret != ERRCODE_SUCC) {
        return;
    }

    /* Service 0: command/report channel. */
    uint16_t h_svc0 = 0;
    ret = ssaps_add_service_sync(g_server_id, &svc_uuid, 1, &h_svc0);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s add_service0 fail 0x%x\r\n", DECOY_LOG, ret);
        return;
    }
    osal_printk("%s svc0 @0x%02x\r\n", DECOY_LOG, h_svc0);

    /* 0x11: notify channel with CCC descriptor. */
    uint16_t h_11 = 0;
    ret = decoy_add_property(
        h_svc0,
        SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE |
            SSAP_OPERATE_INDICATION_BIT_NOTIFY | SSAP_OPERATE_INDICATION_BIT_DESCRITOR_WRITE,
        SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE, g_val_11, VAL11_LEN, &h_11);
    if (ret != ERRCODE_SUCC) {
        return;
    }

    ssaps_desc_info_t desc = {0};
    ret = decoy_add_uuid2(&desc.uuid);
    if (ret != ERRCODE_SUCC) {
        return;
    }
    desc.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    desc.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE;
    desc.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    desc.value_len = sizeof(g_cccd_11);
    desc.value = g_cccd_11;
    ret = ssaps_add_descriptor_sync(g_server_id, h_svc0, h_11, &desc);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s add_descriptor fail 0x%x\r\n", DECOY_LOG, ret);
        return;
    }
    osal_printk("%s   ccc descriptor on 0x%02x\r\n", DECOY_LOG, h_11);

    /* 0x12: output report, 8 bytes. */
    ret = decoy_add_property(
        h_svc0, SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP,
        SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE, g_val_12, VAL12_LEN, NULL);
    if (ret != ERRCODE_SUCC) {
        return;
    }

    /* 0x13: report map, 69 bytes. */
    ret = decoy_add_property(
        h_svc0,
        SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP |
            SSAP_OPERATE_INDICATION_BIT_NOTIFY,
        SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE, g_map_13, MAP13_LEN, NULL);
    if (ret != ERRCODE_SUCC) {
        return;
    }

    /* 0x14: write-only control, 2 bytes. */
    ret = decoy_add_property(h_svc0, SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP,
                             SSAP_PERMISSION_WRITE, g_val_14, VAL14_LEN, NULL);
    if (ret != ERRCODE_SUCC) {
        return;
    }

    /* Service 1: device information. */
    uint16_t h_svc1 = 0;
    ret = ssaps_add_service_sync(g_server_id, &svc_uuid, 1, &h_svc1);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s add_service1 fail 0x%x\r\n", DECOY_LOG, ret);
        return;
    }
    osal_printk("%s svc1 @0x%02x\r\n", DECOY_LOG, h_svc1);

    ret = decoy_add_property(h_svc1, SSAP_OPERATE_INDICATION_BIT_READ, SSAP_PERMISSION_READ,
                             g_val_16, (uint16_t)(sizeof(g_val_16) - 1), NULL);
    if (ret != ERRCODE_SUCC) {
        return;
    }
    ret = decoy_add_property(h_svc1, SSAP_OPERATE_INDICATION_BIT_READ, SSAP_PERMISSION_READ,
                             g_val_17, sizeof(g_val_17), NULL);
    if (ret != ERRCODE_SUCC) {
        return;
    }
    ret = decoy_add_property(h_svc1, SSAP_OPERATE_INDICATION_BIT_READ, SSAP_PERMISSION_READ,
                             g_val_18, (uint16_t)(sizeof(g_val_18) - 1), NULL);
    if (ret != ERRCODE_SUCC) {
        return;
    }

    osal_printk("%s service table ready (%u tracked attrs)\r\n", DECOY_LOG, g_attr_cnt);
}
