#include "securec.h"
#include "soc_osal.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_beacon.h"

#define BEACON_LOG "[sle beacon]"

#define SLE_ADV_HANDLE_DEFAULT 1
#define SLE_ADV_DATA_LEN_MAX 251
#define SLE_CONN_INTV_MIN_DEFAULT 0x64
#define SLE_CONN_INTV_MAX_DEFAULT 0x64
#define SLE_ADV_INTERVAL_MIN_DEFAULT 0xC8
#define SLE_ADV_INTERVAL_MAX_DEFAULT 0xC8
#define SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT 0x1F4
#define SLE_CONN_MAX_LATENCY 0x1F3
#define SLE_ADV_TX_POWER 10
#define NAME_MAX_LENGTH 16

static uint8_t g_sle_local_name[NAME_MAX_LENGTH] = "hello_server";

static uint16_t sle_beacon_set_local_name(uint8_t *adv_data, uint16_t max_len) {
    uint8_t index = 0;
    uint8_t *local_name = g_sle_local_name;
    uint8_t local_name_len = sizeof(g_sle_local_name) - 1;
    adv_data[index++] = local_name_len + 1;
    adv_data[index++] = SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    errno_t ret = memcpy_s(&adv_data[index], max_len - index, local_name, local_name_len);
    if (ret != EOK) {
        return 0;
    }
    return (uint16_t)index + local_name_len;
}

static uint16_t sle_beacon_set_adv_data(uint8_t *adv_data) {
    size_t len = sizeof(struct sle_adv_common_value);
    uint16_t idx = 0;
    struct sle_adv_common_value adv_disc_level = {
        .length = len - 1,
        .type = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL,
        .value = SLE_ANNOUNCE_LEVEL_NORMAL,
    };
    errno_t ret = memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_disc_level, len);
    if (ret != EOK) {
        return 0;
    }
    idx += len;

    /* NearLink system service UUID 0x0B06 (complete 16-bit UUID list). */
    const uint8_t svc_uuid[] = {
        0x02, SLE_ADV_DATA_TYPE_COMPLETE_LIST_OF_16BIT_SERVICE_UUIDS, 0x06, 0x0B
    };
    ret = memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, svc_uuid, sizeof(svc_uuid));
    if (ret != EOK) {
        return 0;
    }
    idx += (uint16_t)sizeof(svc_uuid);
    return idx;
}

static uint16_t sle_beacon_set_scan_response_data(uint8_t *scan_rsp_data) {
    uint16_t idx = 0;
    size_t len = sizeof(struct sle_adv_common_value);
    struct sle_adv_common_value tx_power_level = {
        .length = len - 1,
        .type = SLE_ADV_DATA_TYPE_TX_POWER_LEVEL,
        .value = SLE_ADV_TX_POWER,
    };
    errno_t ret = memcpy_s(scan_rsp_data, SLE_ADV_DATA_LEN_MAX, &tx_power_level, len);
    if (ret != EOK) {
        return 0;
    }
    idx += len;
    idx += sle_beacon_set_local_name(&scan_rsp_data[idx], SLE_ADV_DATA_LEN_MAX - idx);
    return idx;
}

static errcode_t sle_beacon_set_announce_param(void) {
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
    param.announce_tx_power = SLE_ADV_TX_POWER;
    param.own_addr.type = 0;
    errno_t ret = memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, local_addr, SLE_ADDR_LEN);
    if (ret != EOK) {
        return ERRCODE_FAIL;
    }
    return sle_set_announce_param(param.announce_handle, &param);
}

static errcode_t sle_beacon_set_announce_data(void) {
    uint8_t announce_data[SLE_ADV_DATA_LEN_MAX] = {0};
    uint8_t seek_rsp_data[SLE_ADV_DATA_LEN_MAX] = {0};
    sle_announce_data_t data = {0};
    data.announce_data = announce_data;
    data.announce_data_len = sle_beacon_set_adv_data(announce_data);
    data.seek_rsp_data = seek_rsp_data;
    data.seek_rsp_data_len = sle_beacon_set_scan_response_data(seek_rsp_data);
    return sle_set_announce_data(SLE_ADV_HANDLE_DEFAULT, &data);
}

static errcode_t sle_beacon_start(void) {
    errcode_t ret = sle_beacon_set_announce_param();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s set_announce_param fail:%x\r\n", BEACON_LOG, ret);
        return ret;
    }
    ret = sle_beacon_set_announce_data();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s set_announce_data fail:%x\r\n", BEACON_LOG, ret);
        return ret;
    }
    ret = sle_start_announce(SLE_ADV_HANDLE_DEFAULT);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s start_announce fail:%x\r\n", BEACON_LOG, ret);
        return ret;
    }
    osal_printk("%s start announce success.\r\n", BEACON_LOG);
    return ERRCODE_SLE_SUCCESS;
}

static void sle_beacon_enable_cb(errcode_t status) {
    osal_printk("%s enable_cb status=%x\r\n", BEACON_LOG, status);
    if (status == ERRCODE_SLE_SUCCESS) {
        sle_beacon_start();
    }
}

static void sle_beacon_announce_enable_cbk(uint32_t announce_id, errcode_t status) {
    osal_printk("%s announce enable cbk id:%x status:%x\r\n", BEACON_LOG, announce_id, status);
}

static void sle_beacon_announce_disable_cbk(uint32_t announce_id, errcode_t status) {
    osal_printk("%s announce disable cbk id:%x status:%x\r\n", BEACON_LOG, announce_id, status);
}

static void sle_beacon_announce_terminal_cbk(uint32_t announce_id) {
    osal_printk("%s announce terminal cbk id:%x\r\n", BEACON_LOG, announce_id);
}

errcode_t sle_beacon_init(void) {
    sle_announce_seek_callbacks_t cbk = {0};
    cbk.sle_enable_cb = sle_beacon_enable_cb;
    cbk.announce_enable_cb = sle_beacon_announce_enable_cbk;
    cbk.announce_disable_cb = sle_beacon_announce_disable_cbk;
    cbk.announce_terminal_cb = sle_beacon_announce_terminal_cbk;
    errcode_t ret = sle_announce_seek_register_callbacks(&cbk);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register cbk fail:%x\r\n", BEACON_LOG, ret);
        return ret;
    }
    ret = enable_sle();
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s enable_sle fail:%x\r\n", BEACON_LOG, ret);
        return -1;
    }
    osal_printk("%s init ok\r\n", BEACON_LOG);
    return ERRCODE_SLE_SUCCESS;
}