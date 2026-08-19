#include "soc_osal.h"
#include "pinctrl.h"
#include "gpio.h"
#include "chip_io.h"
#include "securec.h"
#include "string.h"
#include "errcode.h"
#include "sle_common.h"
#include "sle_device_manager.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"

#define SCAN_TABLE_SIZE  32
#define SCAN_PRINT_MS    2000

#define CONNECT_TARGET_ADDR  { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x01 }

typedef enum {
    CONN_STATE_SCAN = 0,
    CONN_STATE_CONNECTING,
    CONN_STATE_PAIRING,
    CONN_STATE_ACTIVE,
} conn_state_t;

typedef struct {
    sle_addr_t addr;
    int8_t rssi;
    uint32_t count;
    uint8_t used;
} scan_device_t;

static scan_device_t g_scan_table[SCAN_TABLE_SIZE] = { 0 };
static bool g_table_full = false;
static sle_dev_manager_callbacks_t g_dev_cbk = { 0 };
static sle_announce_seek_callbacks_t g_seek_cbk = { 0 };
static sle_connection_callbacks_t g_conn_cbk = { 0 };

static conn_state_t g_conn_state = CONN_STATE_SCAN;
static bool g_target_locked = false;
static sle_addr_t g_target_addr = { 0 };
static const uint8_t g_target_mac[SLE_ADDR_LEN] = CONNECT_TARGET_ADDR;

static void bs21_rst(void)
{
    uapi_pin_set_mode(S_MGPIO21, (pin_mode_t)HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(S_MGPIO21, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(S_MGPIO21, PIN_PULL_UP);
    reg16_setbits(0x5702C51C, 4, 5, 21);
    reg16_clrbit(0x5702C51C, 0);
}

static scan_device_t *table_find(const sle_addr_t *addr)
{
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (g_scan_table[i].used &&
            memcmp(g_scan_table[i].addr.addr, addr->addr, SLE_ADDR_LEN) == 0) {
            return &g_scan_table[i];
        }
    }
    return NULL;
}

static scan_device_t *table_add(const sle_addr_t *addr)
{
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (!g_scan_table[i].used) {
            g_scan_table[i].used = 1;
            memcpy_s(g_scan_table[i].addr.addr, SLE_ADDR_LEN, addr->addr, SLE_ADDR_LEN);
            return &g_scan_table[i];
        }
    }
    g_table_full = true;
    return NULL;
}

static void conn_rescan(void)
{
    g_target_locked = false;
    g_conn_state = CONN_STATE_SCAN;
    osal_printk("[conn] rescan\r\n");
    sle_start_seek();
}

static void seek_result_cb(sle_seek_result_info_t *result)
{
    scan_device_t *dev;
    if (result == NULL) {
        return;
    }
    dev = table_find(&result->addr);
    if (dev == NULL) {
        dev = table_add(&result->addr);
        if (dev == NULL) {
            return;
        }
    }
    dev->count++;
    dev->rssi = result->rssi;
    if (!g_target_locked &&
        memcmp(result->addr.addr, g_target_mac, SLE_ADDR_LEN) == 0) {
        memcpy_s(&g_target_addr, sizeof(g_target_addr),
                 &result->addr, sizeof(g_target_addr));
        g_target_locked = true;
        osal_printk("[conn] target locked, stopping seek\r\n");
        sle_stop_seek();
    }
}

static void print_scan_table(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (g_scan_table[i].used) {
            n++;
        }
    }
    osal_printk("[scan] devices:%u\r\n", n);
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (g_scan_table[i].used) {
            osal_printk("  %u) %02x:%02x:%02x:%02x:%02x:%02x rssi:%d cnt:%u\r\n",
                        i,
                        g_scan_table[i].addr.addr[0], g_scan_table[i].addr.addr[1],
                        g_scan_table[i].addr.addr[2], g_scan_table[i].addr.addr[3],
                        g_scan_table[i].addr.addr[4], g_scan_table[i].addr.addr[5],
                        g_scan_table[i].rssi, g_scan_table[i].count);
        }
    }
    if (g_table_full) {
        osal_printk("[scan] table full\r\n");
    }
}

static void *scan_task(const char *arg)
{
    while (1) {
        osal_msleep(SCAN_PRINT_MS);
        print_scan_table();
    }
    return NULL;
}

static void conn_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
                                  sle_acb_state_t conn_state, sle_pair_state_t pair_state,
                                  sle_disc_reason_t disc_reason)
{
    osal_printk("[conn] conn id:%u state:%d pair:%d disc:0x%x\r\n",
                conn_id, conn_state, pair_state, disc_reason);
    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        if (pair_state == SLE_PAIR_NONE) {
            g_conn_state = CONN_STATE_PAIRING;
            osal_printk("[conn] pairing...\r\n");
            if (sle_pair_remote_device(addr) != ERRCODE_SUCC) {
                osal_printk("[conn] pair request fail\r\n");
                g_conn_state = CONN_STATE_ACTIVE;
            }
        } else {
            g_conn_state = CONN_STATE_ACTIVE;
        }
    } else {
        conn_rescan();
    }
}

static void pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    osal_printk("[conn] paired: 0x%x\r\n", status);
    g_conn_state = CONN_STATE_ACTIVE;
    if (status == ERRCODE_SUCC) {
        osal_printk("[conn] pairing done, keep connection\r\n");
        sle_connection_param_update_t up = { 0 };
        up.conn_id = conn_id;
        up.interval_min = 100;
        up.interval_max = 100;
        up.max_latency = 0;
        up.supervision_timeout = 200;
        osal_printk("[conn] sending param update (superv=2s)\r\n");
        if (sle_update_connect_param(&up) != ERRCODE_SUCC) {
            osal_printk("[conn] param update send fail\r\n");
        }
    }
}

static void auth_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status,
                             const sle_auth_info_evt_t *evt)
{
    osal_printk("[conn] auth complete: id:%u status:0x%x\r\n", conn_id, status);
    if (status != ERRCODE_SUCC || evt == NULL) {
        return;
    }
    sle_addr_t own_addr = { 0 };
    if (sle_get_local_addr(&own_addr) != ERRCODE_SUCC) {
        osal_printk("[conn] get local addr fail, skip key save\r\n");
        return;
    }
    if (sle_set_nv_smp_keys((sle_auth_info_evt_t *)evt, &own_addr,
                            (sle_addr_t *)addr, 0) != ERRCODE_SUCC) {
        osal_printk("[conn] save smp keys fail\r\n");
        return;
    }
    osal_printk("[conn] smp keys saved\r\n");
}

static void conn_param_update_cb(uint16_t conn_id, errcode_t status,
                                 const sle_connection_param_update_evt_t *param)
{
    osal_printk("[conn] param update result: id:%u status:0x%x interval:%u latency:%u superv:%u\r\n",
                conn_id, status,
                (param ? param->interval : 0),
                (param ? param->latency : 0),
                (param ? param->supervision : 0));
}

static void seek_disable_cb(errcode_t status)
{
    osal_printk("[conn] seek disabled: 0x%x\r\n", status);
    if (status != ERRCODE_SUCC) {
        osal_msleep(100);
        conn_rescan();
        return;
    }
    g_conn_state = CONN_STATE_CONNECTING;
    osal_printk("[conn] connecting...\r\n");
    if (sle_connect_remote_device(&g_target_addr) != ERRCODE_SUCC) {
        osal_printk("[conn] connect fail\r\n");
        conn_rescan();
    }
}

static void scan_start(void)
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

static void sle_power_on_cb(uint8_t status)
{
    osal_printk("sle power on: %d\r\n", status);
    enable_sle();
}

static void *delayed_scan_task(const char *arg)
{
    osal_msleep(3000);
    scan_start();
    return NULL;
}

static void sle_enable_cb(uint8_t status)
{
    sle_addr_t la;
    osal_printk("sle enable: %d\r\n", status);
    if (sle_get_local_addr(&la) == ERRCODE_SUCC) {
        osal_printk("[conn] local addr: %02x:%02x:%02x:%02x:%02x:%02x type:%d\r\n",
                    la.addr[0], la.addr[1], la.addr[2],
                    la.addr[3], la.addr[4], la.addr[5], la.type);
    }
    memset(&la, 0, sizeof(la));
    la.type = SLE_ADDRESS_TYPE_PUBLIC;
    la.addr[0] = 0xaa; la.addr[1] = 0xbb; la.addr[2] = 0xcc;
    la.addr[3] = 0xdd; la.addr[4] = 0xee; la.addr[5] = 0x02;
    if (sle_set_local_addr(&la) == ERRCODE_SUCC) {
        osal_printk("[conn] local addr set to aa:bb:cc:dd:ee:02\r\n");
    }
    sle_announce_seek_register_callbacks(&g_seek_cbk);
    osal_task *t = osal_kthread_create((osal_kthread_handler)delayed_scan_task, 0,
                                       "dscan", 0x1000);
    if (t != NULL) {
        osal_kfree(t);
    }
}

static void seek_enable_cb(errcode_t status)
{
    osal_printk("seek enable: 0x%x\r\n", status);
}

void axk_main(void)
{
    osal_printk("app: sle_pair\r\n");
    bs21_rst();

    g_dev_cbk.sle_power_on_cb = sle_power_on_cb;
    g_dev_cbk.sle_enable_cb = sle_enable_cb;
    sle_dev_manager_register_callbacks(&g_dev_cbk);

    g_seek_cbk.seek_enable_cb = seek_enable_cb;
    g_seek_cbk.seek_result_cb = seek_result_cb;
    g_seek_cbk.seek_disable_cb = seek_disable_cb;

    g_conn_cbk.connect_state_changed_cb = conn_state_changed_cb;
    g_conn_cbk.pair_complete_cb = pair_complete_cb;
    g_conn_cbk.auth_complete_cb = auth_complete_cb;
    g_conn_cbk.connect_param_update_cb = conn_param_update_cb;
    sle_connection_register_callbacks(&g_conn_cbk);

    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)scan_task, 0, "scan_task", 0x1000);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, 24);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();

    enable_sle();
}