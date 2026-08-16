#include "soc_osal.h"
#include "pinctrl.h"
#include "gpio.h"
#include "chip_io.h"
#include "errcode.h"
#include "sle_device_manager.h"
#include "sle_device_discovery.h"

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

static void scan_start(void)
{
    sle_seek_param_t param = { 0 };
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = 1;
    param.seek_type[0] = 1;
    param.seek_interval[0] = 100;
    param.seek_window[0] = 100;
    sle_set_seek_param(&param);
    sle_start_seek();
}

static void sle_power_on_cb(uint8_t status)
{
    osal_printk("sle power on: %d\r\n", status);
    enable_sle();
}

static void sle_enable_cb(uint8_t status)
{
    osal_printk("sle enable: %d\r\n", status);
    sle_announce_seek_register_callbacks(&g_seek_cbk);
    scan_start();
}

static void seek_enable_cb(errcode_t status)
{
    osal_printk("seek enable: 0x%x\r\n", status);
}

static void seek_result_cb(sle_seek_result_info_t *result)
{
    if (result == NULL) {
        return;
    }
    osal_printk("adv %02x:%02x:%02x:%02x:%02x:%02x rssi:%d\r\n",
                result->addr.addr[0], result->addr.addr[1], result->addr.addr[2],
                result->addr.addr[3], result->addr.addr[4], result->addr.addr[5],
                result->rssi);
    for (uint8_t i = 0; i < result->data_length; i++) {
        osal_printk("%02x ", result->data[i]);
    }
    osal_printk("\r\n");
}

void axk_main(void)
{
    bs21_rst();

    g_dev_cbk.sle_power_on_cb = sle_power_on_cb;
    g_dev_cbk.sle_enable_cb = sle_enable_cb;
    sle_dev_manager_register_callbacks(&g_dev_cbk);

    g_seek_cbk.seek_enable_cb = seek_enable_cb;
    g_seek_cbk.seek_result_cb = seek_result_cb;

    enable_sle();
}
