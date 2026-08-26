#include "decoy_server.h"
#include "securec.h"
#include "sle_common.h"
#include "sle_errcode.h"
#include "sle_low_latency.h"
#include "sle_ssap_server.h"
#include "sle_ssap_stru.h"
#include "soc_osal.h"

/* *****************************************************************************
 * Macros
 * *****************************************************************************/

#define DECOY_LOG "[decoy]"
#define VAL11_LEN 4
#define VAL12_LEN 8
#define MAP13_LEN 69
#define VAL14_LEN 2
#define MAX_ATTRS 8

/* Handle padding (A/B experiment): occupies 0x02-0x0F so the mirrored table
 * lands on the real controller's handles — needed IF the dongle hardcodes
 * handles. Cost: 14 extra handle+type pairs not on the real controller. */
#define DECOY_ENABLE_PAD 0

/* *****************************************************************************
 * Attribute storage — mirrors the real controller layout (experiment N)
 * *****************************************************************************/

/* Default values captured from the real controller (experiment N). */
static uint8_t g_val_11[VAL11_LEN];
static uint8_t g_cccd_11[2] = {0x02, 0x00}; /* controller preset: indication mode */
static uint8_t g_val_12[VAL12_LEN] = {0x01, 0x01, 0x11, 0x00,
                                      0x00, 0x00, 0x00, 0x00}; /* last read back (exp N) */
static uint8_t g_val_14[VAL14_LEN] = {0x06, 0x00};

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

static attr_entry_t g_attrs[MAX_ATTRS];
static uint8_t g_attr_cnt = 0;

static uint8_t g_server_id = 0;
static uint16_t g_notify_hdl = 0;

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

static errcode_t decoy_add_uuid2(sle_uuid_t *uuid, uint8_t b1) {
    uuid->len = 2;
    uuid->uuid[0] = 0x37;
    uuid->uuid[1] = b1;
    return ERRCODE_SUCC;
}

/* Register one property and record its handle in the lookup table. */
static errcode_t decoy_add_property(uint16_t svc_hdl, uint32_t oper, uint16_t perms, uint8_t *buf,
                                    uint16_t len, uint16_t *hdl_out) {
    ssaps_property_info_t prop = {0};
    errcode_t ret = decoy_add_uuid2(&prop.uuid, 0xBE);
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

static void decoy_add_service_cb(uint8_t server_id, sle_uuid_t *uuid, uint16_t handle,
                                 errcode_t status) {
    osal_printk("%s add_service cb: sid=%u hdl=0x%x status=0x%x\r\n", DECOY_LOG, server_id, handle,
                status);
}

static void decoy_add_property_cb(uint8_t server_id, sle_uuid_t *uuid, uint16_t service_handle,
                                  uint16_t property_handle, errcode_t status) {
    osal_printk("%s add_property cb: sid=%u svc=0x%x prop=0x%x status=0x%x\r\n", DECOY_LOG,
                server_id, service_handle, property_handle, status);
}

static void decoy_add_descriptor_cb(uint8_t server_id, sle_uuid_t *uuid, uint16_t service_handle,
                                    uint16_t desc_handle, errcode_t status) {
    osal_printk("%s add_desc cb: sid=%u svc=0x%x desc=0x%x status=0x%x\r\n", DECOY_LOG, server_id,
                service_handle, desc_handle, status);
}

static void decoy_start_service_cb(uint8_t server_id, uint16_t handle, errcode_t status) {
    osal_printk("%s start_service cb: sid=%u hdl=0x%x status=0x%x\r\n", DECOY_LOG, server_id,
                handle, status);
}

static void decoy_indicate_cfm_cb(uint8_t server_id, uint16_t conn_id,
                                  sle_indication_cfm_result_t cfm_result, errcode_t status) {
    osal_printk("%s indicate cfm: sid=%u conn=%u cfm=%d status=0x%x\r\n", DECOY_LOG, server_id,
                conn_id, cfm_result, status);
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
 * Low-latency server callbacks — the dongle switches the link into
 * low-latency EM mode right after pairing; without these handlers the
 * stack aborts the link (disc 0x7). Mirrors the official air-mouse flow.
 * *****************************************************************************/

static uint8_t *decoy_hid_data_cb(uint8_t *length, uint16_t *ssap_handle, uint8_t *data_type,
                                  uint16_t co_handle) {
    static uint8_t ll_buf[VAL11_LEN];
    memset_s(ll_buf, sizeof(ll_buf), 0, sizeof(ll_buf));
    if (length != NULL) {
        *length = sizeof(ll_buf);
    }
    if (ssap_handle != NULL) {
        *ssap_handle = g_notify_hdl;
    }
    if (data_type != NULL) {
        *data_type = SSAP_PROPERTY_TYPE_VALUE;
    }
    osal_printk("%s LL hid_data_cb: co=%u len=8\r\n", DECOY_LOG, co_handle);
    return ll_buf;
}

static void decoy_set_em_data_cb(uint16_t co_handle, uint8_t status) {
    osal_printk("%s *** LL em_data switch: co=%u status=%u\r\n", DECOY_LOG, co_handle, status);
}

void decoy_low_latency_init(void) {
    sle_low_latency_callbacks_t cbks = {0};
    cbks.hid_data_cb = decoy_hid_data_cb;
    cbks.sle_set_em_data_cb = decoy_set_em_data_cb;
    errcode_t ret = sle_low_latency_register_callbacks(&cbks);
    osal_printk("%s low_latency_register_callbacks: 0x%x\r\n", DECOY_LOG, ret);
}

/* *****************************************************************************
 * Service registration — mirrors the controller attribute table
 * *****************************************************************************/

/* Register SSAP callbacks. Must run BEFORE enable_sle() (official flow). */
void decoy_server_early_init(void) {
    ssaps_callbacks_t cbks = {0};
    cbks.add_service_cb = decoy_add_service_cb;
    cbks.add_property_cb = decoy_add_property_cb;
    cbks.add_descriptor_cb = decoy_add_descriptor_cb;
    cbks.start_service_cb = decoy_start_service_cb;
    cbks.read_request_cb = decoy_read_cb;
    cbks.write_request_cb = decoy_write_cb;
    cbks.indicate_cfm_cb = decoy_indicate_cfm_cb;
    cbks.mtu_changed_cb = decoy_mtu_changed_cb;
    errcode_t ret = ssaps_register_callbacks(&cbks);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s register_callbacks fail 0x%x\r\n", DECOY_LOG, ret);
    }
}

/* Register server id and the mirrored attribute table.
 * Must run from the SLE-enable callback, AFTER ssaps_set_info(). */
void decoy_services_add(void) {
    ssap_exchange_info_t info = {0};
    info.mtu_size = 520;
    if (ssaps_set_info(0, &info) != ERRCODE_SUCC) {
        osal_printk("%s set_info fail\r\n", DECOY_LOG);
        return;
    }

    sle_uuid_t app_uuid = {0};
    app_uuid.len = 16;
    errcode_t ret = ssaps_register_server(&app_uuid, &g_server_id);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s register_server fail 0x%x\r\n", DECOY_LOG, ret);
        return;
    }

    sle_uuid_t svc_uuid = {0};
    ret = decoy_add_uuid2(&svc_uuid, 0xBE);
    if (ret != ERRCODE_SUCC) {
        return;
    }

    /* Handle padding (A/B experiment): occupies 0x02-0x0F so the mirrored
     * table lands on the real controller's handles (0x10-0x18) — needed IF
     * the dongle hardcodes handles. Cost: 14 extra handle+type pairs that
     * do not exist on the real controller. Toggle to isolate. */
#if DECOY_ENABLE_PAD
    sle_uuid_t pad_uuid = {0};
    static uint8_t g_pad_vals[14];
    uint16_t h_pad = 0;
    ret = decoy_add_uuid2(&pad_uuid, 0xBF);
    if (ret != ERRCODE_SUCC) {
        return;
    }
    /* Secondary service: occupies handles 0x02-0x0F without showing up in
     * primary-service discovery (the real table has nothing there). */
    ret = ssaps_add_service_sync(g_server_id, &pad_uuid, false, &h_pad);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s add pad service fail 0x%x\r\n", DECOY_LOG, ret);
        return;
    }
    for (uint8_t i = 0; i < sizeof(g_pad_vals); i++) {
        ret = decoy_add_property(h_pad, SSAP_OPERATE_INDICATION_BIT_READ,
                                 SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE, &g_pad_vals[i], 1,
                                 NULL);
        if (ret != ERRCODE_SUCC) {
            osal_printk("%s add pad property %u fail 0x%x\r\n", DECOY_LOG, i, ret);
            return;
        }
    }
    ret = ssaps_start_service(g_server_id, h_pad);
    osal_printk("%s pad svc @0x%02x (14 props), start: 0x%x\r\n", DECOY_LOG, h_pad, ret);
#endif

    /* Service 0: command/report channel. */
    uint16_t h_svc0 = 0;
    ret = ssaps_add_service_sync(g_server_id, &svc_uuid, 1, &h_svc0);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s add_service0 fail 0x%x\r\n", DECOY_LOG, ret);
        return;
    }
    osal_printk("%s svc0 @0x%02x\r\n", DECOY_LOG, h_svc0);
    /* 0x11: notify channel with CCC descriptor. Real controller oper =
     * 0x30d (781); the stack's check_property_info cap at 0x100 is lifted
     * by the byte patch in tools/patch_gle_decoy.py. */
    uint16_t h_11 = 0;
    ret = decoy_add_property(h_svc0, 0x30D, SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE, g_val_11,
                             VAL11_LEN, &h_11);
    if (ret != ERRCODE_SUCC) {
        return;
    }
    g_notify_hdl = h_11;

    ssaps_desc_info_t desc = {0};
    ret = decoy_add_uuid2(&desc.uuid, 0xBE);
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

    /* 0x12: output report, 8 bytes. Real controller oper = 0x5 (READ|WRITE). */
    ret = decoy_add_property(
        h_svc0, SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE,
        SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE, g_val_12, VAL12_LEN, NULL);
    if (ret != ERRCODE_SUCC) {
        return;
    }

    /* 0x13: report map, 69 bytes. Real controller oper = 0xd
     * (READ|WRITE|NOTIFY). */
    ret =
        decoy_add_property(h_svc0,
                           SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE |
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

    /* Start svc0 only AFTER all its properties/descriptors are in — a
     * started service rejects further add_property (PARAM_ERR). */
    ret = ssaps_start_service(g_server_id, h_svc0);
    osal_printk("%s start svc0: 0x%x\r\n", DECOY_LOG, ret);

    /* Service 1: device information. Reuses UUID 37BE like the real
     * controller — the STATUS_ERR previously blamed on duplicate UUIDs
     * was actually caused by svc0 sitting in state 1 (added, never
     * started); ssaps_start_service moves it to state 2. */
    uint16_t h_svc1 = 0;
    ret = decoy_add_uuid2(&svc_uuid, 0xBE);
    if (ret != ERRCODE_SUCC) {
        return;
    }
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

    ret = ssaps_start_service(g_server_id, h_svc1);
    osal_printk("%s start svc1: 0x%x\r\n", DECOY_LOG, ret);
    osal_printk("%s service table ready (%u tracked attrs)\r\n", DECOY_LOG, g_attr_cnt);
}
