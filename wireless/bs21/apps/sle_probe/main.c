#include "soc_osal.h"
#include "sle_device_manager.h"
#include "bs21_util.h"
#include "sle_probe_client.h"

void axk_main(void)
{
    osal_printk("app: sle-probe\r\n");
    bs21_rst();
    probe_init();
    enable_sle();
}
