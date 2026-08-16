#include "soc_osal.h"

void *__real_bt_os_new(unsigned int size);
int __real_uapi_gle_set_adv(void *ctx);

static void *g_bt_os_49;

void *bt_os_49_get(void)
{
    return g_bt_os_49;
}

void *__wrap_bt_os_new(unsigned int size)
{
    void *p = __real_bt_os_new(size);
    if (size == 49) {
        g_bt_os_49 = p;
    }
    return p;
}

int __wrap_uapi_gle_set_adv(void *ctx)
{
    unsigned char *b = (unsigned char *)ctx;
    osal_printk("uapi_gle_set_adv ctx: b[4]=%u b[37]=%u b[38]=%u b[39]=%u b[40]=%u len=%u\r\n",
                b[4], b[37], b[38], b[39], b[40], b[37] | (b[38] << 8));
    return __real_uapi_gle_set_adv(ctx);
}
