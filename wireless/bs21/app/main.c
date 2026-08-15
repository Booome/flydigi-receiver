#include "soc_osal.h"

static void *hello_task(const char *arg)
{
    (void)arg;
    while (1) {
        osal_printk("flydigi app running\r\n");
        osal_msleep(1000);
    }
    return NULL;
}

void axk_main(void)
{
    osal_kthread_lock();
    osal_task *t = osal_kthread_create((osal_kthread_handler)hello_task, 0, "hello", 0x1000);
    if (t != NULL) {
        osal_kthread_set_priority(t, 24);
    }
    osal_kthread_unlock();
}
