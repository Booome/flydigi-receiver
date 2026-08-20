#include "bs21_util.h"
#include "pinctrl.h"
#include "gpio.h"
#include "chip_io.h"
#include "string.h"
#include "errcode.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "soc_osal.h"

/*
 * Enable S_MGPIO21 as a pull-up input. The first three calls are the standard
 * SDK API for that. The two raw register writes below come verbatim from the
 * SDK demo (application/demo/demo.c): the SDK exposes no named macro or API
 * for register 0x5702C51C, so they are kept here as a single shared copy.
 * TODO(hw): verify whether these two writes are actually required on hardware;
 * if the pull-up input works without them, drop them.
 */
void bs21_rst(void)
{
    uapi_pin_set_mode(S_MGPIO21, (pin_mode_t)HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(S_MGPIO21, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(S_MGPIO21, PIN_PULL_UP);
    reg16_setbits(0x5702C51C, 4, 5, 21);
    reg16_clrbit(0x5702C51C, 0);
}

void sle_setup_set_local_addr(void)
{
    sle_addr_t la = { 0 };
    la.type = SLE_ADDRESS_TYPE_PUBLIC;
    la.addr[0] = 0xaa; la.addr[1] = 0xbb; la.addr[2] = 0xcc;
    la.addr[3] = 0xdd; la.addr[4] = 0xee; la.addr[5] = 0x02;
    if (sle_set_local_addr(&la) == ERRCODE_SUCC) {
        osal_printk("[conn] local addr set to aa:bb:cc:dd:ee:02\r\n");
    }
}

void sle_scan_start(void)
{
    sle_seek_param_t param = { 0 };
    errcode_t rc;
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = 1;
    param.seek_type[0] = 1;
    param.seek_interval[0] = 100;
    param.seek_window[0] = 100;
    rc = sle_set_seek_param(&param);
    if (rc != ERRCODE_SUCC) {
        osal_printk("sle_set_seek_param fail: 0x%x\r\n", rc);
        return;
    }
    rc = sle_start_seek();
    if (rc != ERRCODE_SUCC) {
        osal_printk("sle_start_seek fail: 0x%x\r\n", rc);
    }
}
