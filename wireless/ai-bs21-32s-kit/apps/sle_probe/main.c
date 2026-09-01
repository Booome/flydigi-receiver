#include "bs21_util.h"
#include "sle_device_manager.h"
#include "sle_probe_client.h"
#include "soc_osal.h"

void axk_main(void) {
  bs21_rst();
  osal_printk("app: sle-probe\r\n");
  probe_init();
  enable_sle();
}
