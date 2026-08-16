#include "soc_osal.h"
#include "pinctrl.h"
#include "gpio.h"
#include "chip_io.h"
#include "securec.h"
#include "errcode.h"
#include "sle_common.h"
#include "sle_device_manager.h"
#include "sle_device_discovery.h"

extern void *g_intheap_begin;
extern unsigned int LOS_MemTotalUsedGet(void *pool);

#define ADV_NAME             "flydigi_t"
#define ADV_HANDLE           1
#define ADV_DATA_LEN_MAX     251
#define ADV_INTERVAL_MIN     0xC8
#define ADV_INTERVAL_MAX     0xC8
#define CONN_INTV_MIN        0x64
#define CONN_INTV_MAX        0x64
#define CONN_MAX_LATENCY     0x1F3
#define CONN_SUPERVISION_TO  0x1F4

#define ADV_CHANNEL_MAP_DEFAULT            0x07
#define ADV_DATA_TYPE_DISCOVERY_LEVEL      0x01
#define ADV_DATA_TYPE_ACCESS_MODE          0x02
#define ADV_DATA_TYPE_COMPLETE_LOCAL_NAME  0x0B

struct adv_common_value {
    uint8_t type;
    uint8_t length;
    uint8_t value;
};

static sle_dev_manager_callbacks_t g_dev_cbk = { 0 };
static sle_announce_seek_callbacks_t g_seek_cbk = { 0 };

static void bs21_rst(void)
{
    uapi_pin_set_mode(S_MGPIO21, (pin_mode_t)HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(S_MGPIO21, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(S_MGPIO21, PIN_PULL_UP);
    reg16_setbits(0x5702C51C, 4, 5, 21);
    reg16_clrbit(0x5702C51C, 0);
}

static int set_announce_param(void)
{
    sle_announce_param_t param = { 0 };
    unsigned char local_addr[SLE_ADDR_LEN] = { 0 };

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
    uint16_t idx = 0;
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

    announce_data[idx++] = name_len + 1;
    announce_data[idx++] = ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    if (memcpy_s(&announce_data[idx], ADV_DATA_LEN_MAX - idx, name, name_len) != EOK) {
        return -1;
    }
    idx += name_len;

    data.announce_data = announce_data;
    data.announce_data_len = idx;

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
    osal_printk("heap used before announce: %u\r\n", LOS_MemTotalUsedGet(&g_intheap_begin));
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

void axk_main(void)
{
    osal_printk("app: t_broadcaster\r\n");
    osal_printk("heap used: %u\r\n", LOS_MemTotalUsedGet(&g_intheap_begin));
    bs21_rst();

    g_dev_cbk.sle_power_on_cb = sle_power_on_cb;
    g_dev_cbk.sle_enable_cb = sle_enable_cb;
    sle_dev_manager_register_callbacks(&g_dev_cbk);

    g_seek_cbk.announce_enable_cb = announce_enable_cb;

    enable_sle();
}
