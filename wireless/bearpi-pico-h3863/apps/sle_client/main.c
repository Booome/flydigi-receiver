#include "soc_osal.h"
#include "app_init.h"
#include "sle_errcode.h"
#include "sle_client.h"

#define TASK_STACK_SIZE 0x2000
#define TASK_PRIORITY 25

static void *sle_client_task(const char *arg) {
    osal_printk("SLE Client Task started\r\n");

    errcode_t ret = sle_client_init();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("sle_client_init failed: 0x%x\r\n", ret);
        return NULL;
    }

    for (;;) {
        osal_msleep(5000);
    }
    return NULL;
}

static void sle_client_entry(void) {
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle =
        osal_kthread_create((osal_kthread_handler)sle_client_task, 0, "SleClient", TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, TASK_PRIORITY);
    }
    osal_kthread_unlock();
}

app_run(sle_client_entry);
