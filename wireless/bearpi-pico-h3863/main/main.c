#include "soc_osal.h"
#include "app_init.h"

#define TASK_STACK_SIZE 0x1000
#define TASK_PRIO 26

static int hello_task(const char *arg) {
    for (;;) {
        osal_printk("Hello from BearPi-Pico H3863!\r\n");
        osal_msleep(1000);
    }

    return 0;
}

static void hello_entry(void) {
    osal_task *task = NULL;
    osal_kthread_lock();
    task = osal_kthread_create((osal_kthread_handler)hello_task, 0, "HelloTask", TASK_STACK_SIZE);
    if (task != NULL) {
        osal_kthread_set_priority(task, TASK_PRIO);
        osal_kfree(task);
    }
    osal_kthread_unlock();
}

app_run(hello_entry);
