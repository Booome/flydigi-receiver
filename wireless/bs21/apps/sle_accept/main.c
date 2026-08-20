#include "soc_osal.h"
#include "pinctrl.h"
#include "gpio.h"
#include "chip_io.h"
#include "securec.h"
#include "errcode.h"
#include "sle_common.h"
#include "sle_device_manager.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "bs21_util.h"

#define ADV_NAME             "flydigi_t"
#define ADV_HANDLE           1
#define ADV_DATA_LEN_MAX     251
#define ADV_INTERVAL_MIN     0xC8
#define ADV_INTERVAL_MAX     0xC8
#define CONN_INTV_MIN        0x64
#define CONN_INTV_MAX        0x64
#define CONN_MAX_LATENCY     0x1F3
#define CONN_SUPERVISION_TO  0x64

#define ADV_CHANNEL_MAP_DEFAULT            0x07
#define ADV_DATA_TYPE_DISCOVERY_LEVEL      0x01
#define ADV_DATA_TYPE_ACCESS_MODE          0x02
#define ADV_DATA_TYPE_COMPLETE_LOCAL_NAME  0x0B
#define ADV_DATA_TYPE_TX_POWER_LEVEL       0x0C
#define ADV_TX_POWER                       10

struct adv_common_value {
    uint8_t type;
    uint8_t length;
    uint8_t value;
};

static sle_dev_manager_callbacks_t g_dev_cbk = { 0 };
static sle_announce_seek_callbacks_t g_seek_cbk = { 0 };
static sle_connection_callbacks_t g_conn_cbk = { 0 };

static int set_announce_param(void)
{
    sle_announce_param_t param = { 0 };
    unsigned char local_addr[SLE_ADDR_LEN] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x01 };

    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = ADV_HANDLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = ADV_CHANNEL_MAP_DEFAULT;
    param.announce_interval_min = ADV_INTERVAL_MIN;
    param.announce_interval_max = ADV_INTERVAL_MAX;
    param.conn_interval_min = CONN_INTV_MIN;
    param.conn_interval_max = CONN_INTV_MAX;
    param.conn_max_latency = CONN_MAX_LATENCY;
    param.conn_supervision_timeout = CONN_SUPERVISION_TO;
    param.own_addr.type = 0;
    if (memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, local_addr, SLE_ADDR_LEN) != EOK) {
        return -1;
    }
    return sle_set_announce_param(param.announce_handle, &param);
}

static int set_announce_data(void)
{
    sle_announce_data_t data = { 0 };
    uint8_t announce_data[ADV_DATA_LEN_MAX] = { 0 };
    uint8_t seek_rsp_data[ADV_DATA_LEN_MAX] = { 0 };
    uint16_t idx = 0;
    uint16_t rsp_idx = 0;
    const char *name = ADV_NAME;
    uint8_t name_len = sizeof(ADV_NAME) - 1;

    struct adv_common_value disc_level = {
        .type = ADV_DATA_TYPE_DISCOVERY_LEVEL,
        .length = sizeof(struct adv_common_value) - 1,
        .value = SLE_ANNOUNCE_LEVEL_NORMAL,
    };
    if (memcpy_s(&announce_data[idx], ADV_DATA_LEN_MAX - idx,
                 &disc_level, sizeof(disc_level)) != EOK) {
        return -1;
    }
    idx += sizeof(disc_level);

    struct adv_common_value access_mode = {
        .type = ADV_DATA_TYPE_ACCESS_MODE,
        .length = sizeof(struct adv_common_value) - 1,
        .value = 0,
    };
    if (memcpy_s(&announce_data[idx], ADV_DATA_LEN_MAX - idx,
                 &access_mode, sizeof(access_mode)) != EOK) {
        return -1;
    }
    idx += sizeof(access_mode);

    struct adv_common_value tx_power = {
        .type = ADV_DATA_TYPE_TX_POWER_LEVEL,
        .length = sizeof(struct adv_common_value) - 1,
        .value = ADV_TX_POWER,
    };
    if (memcpy_s(&seek_rsp_data[rsp_idx], ADV_DATA_LEN_MAX - rsp_idx,
                 &tx_power, sizeof(tx_power)) != EOK) {
        return -1;
    }
    rsp_idx += sizeof(tx_power);

    seek_rsp_data[rsp_idx++] = name_len + 1;
    seek_rsp_data[rsp_idx++] = ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    if (memcpy_s(&seek_rsp_data[rsp_idx], ADV_DATA_LEN_MAX - rsp_idx, name, name_len) != EOK) {
        return -1;
    }
    rsp_idx += name_len;

    data.announce_data = announce_data;
    data.announce_data_len = idx;
    data.seek_rsp_data = seek_rsp_data;
    data.seek_rsp_data_len = rsp_idx;

    return sle_set_announce_data(ADV_HANDLE, &data);
}

static void announce_enable_cb(uint32_t announce_id, errcode_t status)
{
    osal_printk("announce enable id:%x status:0x%x\r\n", announce_id, status);
}

static void sle_power_on_cb(uint8_t status)
{
    osal_printk("sle power on: %d\r\n", status);
    enable_sle();
}

static void sle_enable_cb(uint8_t status)
{
    int rc;
    errcode_t announce_rc;
    osal_printk("sle enable: %d\r\n", status);
    sle_announce_seek_register_callbacks(&g_seek_cbk);
    rc = set_announce_param();
    if (rc != 0) {
        osal_printk("set_announce_param fail: %d\r\n", rc);
        return;
    }
    rc = set_announce_data();
    if (rc != 0) {
        osal_printk("set_announce_data fail: %d\r\n", rc);
        return;
    }
    announce_rc = sle_start_announce(ADV_HANDLE);
    if (announce_rc != ERRCODE_SUCC) {
        osal_printk("sle_start_announce fail: 0x%x\r\n", announce_rc);
    }
}

static void *re_announce_task(const char *arg)
{
    osal_msleep(5000);
    errcode_t rc = sle_start_announce(ADV_HANDLE);
    osal_printk("[conn] re-announce rc:0x%x\r\n", rc);
    return NULL;
}

static void conn_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
                                  sle_acb_state_t conn_state, sle_pair_state_t pair_state,
                                  sle_disc_reason_t disc_reason)
{
    osal_printk("[conn] id:%u state:%d pair:%d disc:0x%x\r\n",
                conn_id, conn_state, pair_state, disc_reason);
    if (conn_state == SLE_ACB_STATE_DISCONNECTED &&
        disc_reason != SLE_DISCONNECT_BY_LOCAL) {
        osal_task *t = osal_kthread_create((osal_kthread_handler)re_announce_task, 0,
                                           "reann", 0x1000);
        if (t != NULL) {
            osal_kfree(t);
        }
    }
}

static void auth_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status,
                             const sle_auth_info_evt_t *evt)
{
    osal_printk("[conn] auth complete: id:%u status:0x%x\r\n", conn_id, status);
    if (status != ERRCODE_SUCC || evt == NULL) {
        return;
    }
    sle_addr_t own_addr = { 0 };
    if (sle_get_local_addr(&own_addr) != ERRCODE_SUCC) {
        osal_printk("[conn] get local addr fail, skip key save\r\n");
        return;
    }
    if (sle_set_nv_smp_keys((sle_auth_info_evt_t *)evt, &own_addr,
                            (sle_addr_t *)addr, 0) != ERRCODE_SUCC) {
        osal_printk("[conn] save smp keys fail\r\n");
        return;
    }
    osal_printk("[conn] smp keys saved\r\n");
}

void axk_main(void)
{
    osal_printk("app: sle_accept\r\n");
    bs21_rst();

    g_dev_cbk.sle_power_on_cb = sle_power_on_cb;
    g_dev_cbk.sle_enable_cb = sle_enable_cb;
    sle_dev_manager_register_callbacks(&g_dev_cbk);

    g_seek_cbk.announce_enable_cb = announce_enable_cb;
    g_conn_cbk.connect_state_changed_cb = conn_state_changed_cb;
    g_conn_cbk.auth_complete_cb = auth_complete_cb;
    sle_connection_register_callbacks(&g_conn_cbk);

    enable_sle();
}
