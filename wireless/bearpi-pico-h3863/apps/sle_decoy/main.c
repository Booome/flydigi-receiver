#include "soc_osal.h"
#include "app_init.h"
#include "sle_errcode.h"
#include "sle_decoy.h"

#define TASK_STACK_SIZE 0x2000
#define TASK_PRIORITY 25

static void *sle_decoy_task(const char *arg) {
    errcode_t ret = sle_decoy_init();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("sle_decoy_init failed: 0x%x\r\n", ret);
        return NULL;
    }

    for (;;) {
        osal_msleep(5000);
    }
    return NULL;
}

static void sle_decoy_entry(void) {
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle =
        osal_kthread_create((osal_kthread_handler)sle_decoy_task, 0, "SleDecoy", TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, TASK_PRIORITY);
    }
    osal_kthread_unlock();
}

app_run(sle_decoy_entry);
