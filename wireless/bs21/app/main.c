#define __DEMO_C_
#include "demo.h"

static void bs21_rst(void)
{
    uapi_pin_set_mode(S_MGPIO21, (pin_mode_t)HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(S_MGPIO21, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(S_MGPIO21, PIN_PULL_UP);
    reg16_setbits(0x5702C51C, 4, 5, 21);
    reg16_clrbit(0x5702C51C, 0);
}

static void *hello_task(const char *arg)
{
    (void)arg;
    while (1)
    {
        osal_printk("\r\n\r\n flydigi app running \r\n\r\n");
        osal_msleep(1000);
    }

    return NULL;
}

void axk_main(void)
{
    bs21_rst();

    osal_task *task_handle = NULL;

    osal_kthread_lock();

    task_handle = osal_kthread_create((osal_kthread_handler)hello_task, 0, "hello_task", 0x1000);

    if (task_handle != NULL)
    {
        osal_kthread_set_priority(task_handle, 24);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}
