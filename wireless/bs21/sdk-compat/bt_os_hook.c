#include "soc_osal.h"

void *__real_bt_os_new(unsigned int size);

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
