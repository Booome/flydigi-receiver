#include "securec.h"
#include "soc_osal.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_decoy.h"

#define SLE_ADV_INTERVAL_MIN_DEFAULT 0xC8
#define SLE_ADV_INTERVAL_MAX_DEFAULT 0xC8
#define SLE_CONN_INTV_MIN_DEFAULT 0x64
#define SLE_CONN_INTV_MAX_DEFAULT 0x64
#define SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT 0x64
#define SLE_CONN_MAX_LATENCY 0x1F3
#define SLE_ADV_TX_POWER_DBM 10

#define SLE_DECOY_LOG "[sle decoy]"

/* Real controller address (from BS21 seek logs). Dongle binds to this MAC. */
static unsigned char g_local_addr[SLE_ADDR_LEN] = {0xA1, 0xA2, 0xC8, 0x75, 0x43, 0xB8};

/* Captured from the real controller over the air (BS21 decoy). */
static const uint8_t g_adv_data[] = {0x01, 0x01, 0x01, 0x05, 0x04, 0x0B, 0x06, 0x09, 0x06, 0x03,
                                     0x12, 0x09, 0x06, 0x07, 0x03, 0x02, 0x05, 0x00, 0x06, 0x0A,
                                     0x66, 0x6C, 0x79, 0x5F, 0x64, 0x69, 0x67, 0x69, 0x67, 0x31};
static const uint8_t g_adv_rsp[] = {
    0x0B, 0x0A, 0x66, 0x6C, 0x79, 0x5F, 0x64, 0x69, 0x67, 0x69, 0x67, 0x73
};

typedef enum sle_adv_channel {
    SLE_ADV_CHANNEL_MAP_77 = 0x01,
    SLE_ADV_CHANNEL_MAP_78 = 0x02,
    SLE_ADV_CHANNEL_MAP_79 = 0x04,
    SLE_ADV_CHANNEL_MAP_DEFAULT = 0x07
} sle_adv_channel_map_t;

static int sle_set_default_announce_param(void) {
    errno_t ret;
    sle_announce_param_t param = {0};

    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = 1;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = SLE_ADV_CHANNEL_MAP_DEFAULT;
    param.announce_interval_min = SLE_ADV_INTERVAL_MIN_DEFAULT;
    param.announce_interval_max = SLE_ADV_INTERVAL_MAX_DEFAULT;
    param.conn_interval_min = SLE_CONN_INTV_MIN_DEFAULT;
    param.conn_interval_max = SLE_CONN_INTV_MAX_DEFAULT;
    param.conn_max_latency = SLE_CONN_MAX_LATENCY;
    param.conn_supervision_timeout = SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT;
    param.announce_tx_power = SLE_ADV_TX_POWER_DBM;
    param.own_addr.type = 0;
    ret = memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, g_local_addr, SLE_ADDR_LEN);
    if (ret != EOK) {
        return 0;
    }
    return sle_set_announce_param(param.announce_handle, &param);
}

static int sle_set_default_announce_data(void) {
    errcode_t ret;
    sle_announce_data_t data = {0};
    uint8_t adv_handle = 1;

    data.announce_data = (uint8_t *)g_adv_data;
    data.announce_data_len = sizeof(g_adv_data);
    data.seek_rsp_data = (uint8_t *)g_adv_rsp;
    data.seek_rsp_data_len = sizeof(g_adv_rsp);

    ret = sle_set_announce_data(adv_handle, &data);
    if (ret == ERRCODE_SLE_SUCCESS) {
        osal_printk("%s set announce data success.\r\n", SLE_DECOY_LOG);
    } else {
        osal_printk("%s set adv param fail.\r\n", SLE_DECOY_LOG);
    }
    return ERRCODE_SLE_SUCCESS;
}

static void sle_decoy_announce_enable_cb(uint32_t announce_id, errcode_t status) {
    osal_printk("%s announce enable id:%02x status:%x\r\n", SLE_DECOY_LOG, announce_id, status);
}

static void sle_decoy_announce_disable_cb(uint32_t announce_id, errcode_t status) {
    osal_printk("%s announce disable id:%02x status:%x\r\n", SLE_DECOY_LOG, announce_id, status);
}

static void sle_decoy_announce_terminal_cb(uint32_t announce_id) {
    osal_printk("%s announce terminal id:%02x\r\n", SLE_DECOY_LOG, announce_id);
}

errcode_t sle_decoy_adv_init(void) {
    errcode_t ret;
    sle_announce_seek_callbacks_t seek_cbks = {0};
    osal_printk("%s adv_init start\r\n", SLE_DECOY_LOG);
    seek_cbks.announce_enable_cb = sle_decoy_announce_enable_cb;
    seek_cbks.announce_disable_cb = sle_decoy_announce_disable_cb;
    seek_cbks.announce_terminal_cb = sle_decoy_announce_terminal_cb;
    ret = sle_announce_seek_register_callbacks(&seek_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register announce callbacks fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    sle_set_default_announce_param();
    sle_set_default_announce_data();
    ret = sle_start_announce(1);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s start_announce fail:%x\r\n", SLE_DECOY_LOG, ret);
        return ret;
    }

    osal_printk("%s start announce success.\r\n", SLE_DECOY_LOG);
    return ERRCODE_SLE_SUCCESS;
}
