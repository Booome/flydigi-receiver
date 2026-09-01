#include "securec.h"
#include "soc_osal.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_server.h"

#define SLE_ADV_TX_POWER_DBM 18

#define SLE_ADV_INTERVAL_MIN_DEFAULT 0xC8
#define SLE_ADV_INTERVAL_MAX_DEFAULT 0xC8
#define SLE_CONN_INTV_MIN_DEFAULT 0x64
#define SLE_CONN_INTV_MAX_DEFAULT 0x64
#define SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT 0x1F4
#define SLE_CONN_MAX_LATENCY 0x1F3

#define SLE_SERVER_NAME "flydigi_m2"

#define SLE_SERVER_LOG "[sle server]"

typedef struct sle_adv_common_value {
    uint8_t length;
    uint8_t type;
    uint8_t value;
} le_adv_common_t;

typedef enum sle_adv_channel {
    SLE_ADV_CHANNEL_MAP_77 = 0x01,
    SLE_ADV_CHANNEL_MAP_78 = 0x02,
    SLE_ADV_CHANNEL_MAP_79 = 0x04,
    SLE_ADV_CHANNEL_MAP_DEFAULT = 0x07
} sle_adv_channel_map_t;

typedef enum sle_adv_data {
    SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL = 0x01,
    SLE_ADV_DATA_TYPE_ACCESS_MODE = 0x02,
    SLE_ADV_DATA_TYPE_SERVICE_DATA_16BIT_UUID = 0x03,
    SLE_ADV_DATA_TYPE_SERVICE_DATA_128BIT_UUID = 0x04,
    SLE_ADV_DATA_TYPE_COMPLETE_LIST_OF_16BIT_SERVICE_UUIDS = 0x05,
    SLE_ADV_DATA_TYPE_COMPLETE_LIST_OF_128BIT_SERVICE_UUIDS = 0x06,
    SLE_ADV_DATA_TYPE_INCOMPLETE_LIST_OF_16BIT_SERVICE_UUIDS = 0x07,
    SLE_ADV_DATA_TYPE_INCOMPLETE_LIST_OF_128BIT_SERVICE_UUIDS = 0x08,
    SLE_ADV_DATA_TYPE_SERVICE_STRUCTURE_HASH_VALUE = 0x09,
    SLE_ADV_DATA_TYPE_SHORTENED_LOCAL_NAME = 0x0A,
    SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME = 0x0B,
    SLE_ADV_DATA_TYPE_TX_POWER_LEVEL = 0x0C,
    SLE_ADV_DATA_TYPE_SLB_COMMUNICATION_DOMAIN = 0x0D,
    SLE_ADV_DATA_TYPE_SLB_MEDIA_ACCESS_LAYER_ID = 0x0E,
    SLE_ADV_DATA_TYPE_EXTENDED = 0xFE,
    SLE_ADV_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA = 0xFF
} sle_adv_data_type;

static uint8_t g_sle_local_name[] = SLE_SERVER_NAME;

static uint16_t sle_set_adv_local_name(uint8_t *adv_data, uint16_t max_len) {
    errno_t ret;
    uint8_t index = 0;
    uint8_t local_name_len = sizeof(g_sle_local_name) - 1;

    adv_data[index++] = local_name_len + 1;
    adv_data[index++] = SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    ret = memcpy_s(&adv_data[index], max_len - index, g_sle_local_name, local_name_len);
    if (ret != EOK) {
        osal_printk("%s memcpy fail\r\n", SLE_SERVER_LOG);
        return 0;
    }
    return (uint16_t)index + local_name_len;
}

static uint16_t sle_set_adv_data(uint8_t *adv_data) {
    size_t len = 0;
    uint16_t idx = 0;
    errno_t ret = 0;

    len = sizeof(struct sle_adv_common_value);
    struct sle_adv_common_value adv_disc_level = {
        .length = len - 1,
        .type = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL,
        .value = SLE_ANNOUNCE_LEVEL_NORMAL,
    };
    ret = memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_disc_level, len);
    if (ret != EOK) {
        return 0;
    }
    idx += len;

    len = sizeof(struct sle_adv_common_value);
    struct sle_adv_common_value adv_access_mode = {
        .length = len - 1,
        .type = SLE_ADV_DATA_TYPE_ACCESS_MODE,
        .value = 0,
    };
    ret = memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_access_mode, len);
    if (ret != EOK) {
        return 0;
    }
    idx += len;

    return idx;
}

static uint16_t sle_set_scan_response_data(uint8_t *scan_rsp_data) {
    uint16_t idx = 0;
    errno_t ret;
    size_t scan_rsp_data_len = sizeof(struct sle_adv_common_value);

    struct sle_adv_common_value tx_power_level = {
        .length = scan_rsp_data_len - 1,
        .type = SLE_ADV_DATA_TYPE_TX_POWER_LEVEL,
        .value = SLE_ADV_TX_POWER_DBM,
    };
    ret = memcpy_s(scan_rsp_data, SLE_ADV_DATA_LEN_MAX, &tx_power_level, scan_rsp_data_len);
    if (ret != EOK) {
        return 0;
    }
    idx += scan_rsp_data_len;

    idx += sle_set_adv_local_name(&scan_rsp_data[idx], SLE_ADV_DATA_LEN_MAX - idx);
    return idx;
}

static int sle_set_default_announce_param(void) {
    errno_t ret;
    sle_announce_param_t param = {0};
    unsigned char local_addr[SLE_ADDR_LEN] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = SLE_ADV_HANDLE_DEFAULT;
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
    ret = memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, local_addr, SLE_ADDR_LEN);
    if (ret != EOK) {
        return 0;
    }
    return sle_set_announce_param(param.announce_handle, &param);
}

static int sle_set_default_announce_data(void) {
    errcode_t ret;
    uint8_t announce_data_len = 0;
    uint8_t seek_data_len = 0;
    sle_announce_data_t data = {0};
    uint8_t adv_handle = SLE_ADV_HANDLE_DEFAULT;
    uint8_t announce_data[SLE_ADV_DATA_LEN_MAX] = {0};
    uint8_t seek_rsp_data[SLE_ADV_DATA_LEN_MAX] = {0};

    announce_data_len = sle_set_adv_data(announce_data);
    data.announce_data = announce_data;
    data.announce_data_len = announce_data_len;

    seek_data_len = sle_set_scan_response_data(seek_rsp_data);
    data.seek_rsp_data = seek_rsp_data;
    data.seek_rsp_data_len = seek_data_len;

    ret = sle_set_announce_data(adv_handle, &data);
    if (ret == ERRCODE_SLE_SUCCESS) {
        osal_printk("%s set announce data success.\r\n", SLE_SERVER_LOG);
    } else {
        osal_printk("%s set adv param fail.\r\n", SLE_SERVER_LOG);
    }
    return ERRCODE_SLE_SUCCESS;
}

static void sle_server_announce_enable_cbk(uint32_t announce_id, errcode_t status) {
    osal_printk("%s announce enable cbk id:%02x, status:%x\r\n", SLE_SERVER_LOG, announce_id,
                status);
}

static void sle_server_announce_disable_cbk(uint32_t announce_id, errcode_t status) {
    osal_printk("%s announce disable cbk id:%02x, status:%x\r\n", SLE_SERVER_LOG, announce_id,
                status);
}

static void sle_server_announce_terminal_cbk(uint32_t announce_id) {
    osal_printk("%s announce terminal cbk id:%02x\r\n", SLE_SERVER_LOG, announce_id);
}

errcode_t sle_server_announce_register_cbks(void) {
    errcode_t ret = 0;
    sle_announce_seek_callbacks_t seek_cbks = {0};
    seek_cbks.announce_enable_cb = sle_server_announce_enable_cbk;
    seek_cbks.announce_disable_cb = sle_server_announce_disable_cbk;
    seek_cbks.announce_terminal_cb = sle_server_announce_terminal_cbk;
    ret = sle_announce_seek_register_callbacks(&seek_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register announce callbacks fail:%x\r\n", SLE_SERVER_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
}

errcode_t sle_server_adv_init(void) {
    errcode_t ret;
    sle_set_default_announce_param();
    sle_set_default_announce_data();
    ret = sle_start_announce(SLE_ADV_HANDLE_DEFAULT);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s sle_start_announce fail:%x\r\n", SLE_SERVER_LOG, ret);
        return ret;
    }
    osal_printk("%s start announce success.\r\n", SLE_SERVER_LOG);
    return ERRCODE_SLE_SUCCESS;
}
