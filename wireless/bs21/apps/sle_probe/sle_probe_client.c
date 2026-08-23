#include "sle_probe_client.h"
#include "bs21_util.h"
#include "scan_table.h"
#include "securec.h"
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_device_manager.h"
#include "sle_low_latency.h"
#include "sle_ssap_client.h"
#include "soc_osal.h"
#include "systick.h"

/* *****************************************************************************
 * Macros
 * *****************************************************************************/

#define PROBE_LOG "[probe]"
#define PROBE_SCAN_MS 5000
#define PROBE_MTU_SIZE_DEFAULT 520

/* *****************************************************************************
 * Global state
 * *****************************************************************************/

static ssapc_callbacks_t g_ssapc_cbk = {0};
static sle_dev_manager_callbacks_t g_dev_cbk = {0};
static sle_announce_seek_callbacks_t g_seek_cbk = {0};
static sle_connection_callbacks_t g_conn_cbk = {0};

static sle_addr_t g_target_addr = {0};
uint16_t g_conn_id = 0;
uint8_t g_client_id = 0;
static uint64_t g_scan_start_ms = 0;

/* Discovered property handles, grouped by capability. */
static uint16_t g_write_hdls[8];
static uint8_t g_write_cnt = 0;
static uint16_t g_notify_hdls[8];
static uint8_t g_notify_cnt = 0;
static uint16_t g_all_hdls[8];
static uint8_t g_all_cnt = 0;
static uint16_t g_cmd_hdls[8];
static uint8_t g_cmd_cnt = 0;
static uint8_t g_desc_types[8][4];
static uint8_t g_desc_cnt[8];

/* SSAP find sequence: top-level types first, then per-service sub-elements. */
static const uint8_t g_find_types[] = {
    SSAP_FIND_TYPE_PROPERTY,
    SSAP_FIND_TYPE_SERVICE_STRUCTURE,
    SSAP_FIND_TYPE_PRIMARY_SERVICE,
};
#define FIND_TYPE_COUNT (sizeof(g_find_types) / sizeof(g_find_types[0]))
static uint8_t g_find_idx = 0;

/* Sub-element types queried per primary service (require UUID context). */
static const uint8_t g_sub_types[] = {
    SSAP_FIND_TYPE_REFERENCE_SERVICE,
    SSAP_FIND_TYPE_METHOD,
    SSAP_FIND_TYPE_EVENT,
};
#define SUB_TYPE_COUNT (sizeof(g_sub_types) / sizeof(g_sub_types[0]))

/* Discovered primary services. */
#define MAX_SERVICES 8
static ssapc_find_service_result_t g_primary_services[MAX_SERVICES];
static uint8_t g_primary_cnt = 0;
static uint8_t g_service_idx = 0;
static uint8_t g_sub_idx = 0;

/* The find query currently in flight (logged by callbacks, used by
 * find_structure_cb to decide whether to record service entries). */
static uint8_t g_cur_find_type = 0xFF;

/* Experiment phase synchronization. */
static volatile int g_discovery_done = 0;
static volatile int g_disconnected = 0;
static osal_task *g_exp_task = NULL;

/* Validate client_id and conn_id in SSAP callbacks. */
#define CB_CHK()                                                                                   \
    do {                                                                                           \
        if (client_id != g_client_id || conn_id != g_conn_id) {                                    \
            osal_printk("%s CB MISMATCH c=%u/%u conn=%u/%u at %s:%u\r\n", PROBE_LOG, client_id,    \
                        g_client_id, conn_id, g_conn_id, __FILE__, __LINE__);                      \
        }                                                                                          \
    } while (0)

/* *****************************************************************************
 * Forward declarations
 * *****************************************************************************/

static void probe_start_scan(void);
static void probe_connect_best(void);
static void probe_sle_enable_cb(uint8_t status);
static void probe_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status);
static void ssapc_exchange_info_cb(uint8_t client_id, uint16_t conn_id, ssap_exchange_info_t *param,
                                   errcode_t status);
static void start_next_find(void);

/* *****************************************************************************
 * Helpers
 * *****************************************************************************/

static void probe_print_hex(const uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        osal_printk("%02x ", buf[i]);
    }
    osal_printk("\r\n");
}

static void probe_log_frame(const char *tag, const ssapc_handle_value_t *data) {
    osal_printk("%s %s len=%u ", PROBE_LOG, tag, data->data_len);
    probe_print_hex(data->data, data->data_len);
    if (data->data_len >= 3 && data->data[0] == 0x5a && data->data[1] == 0xa5) {
        if (data->data[2] == 0xef) {
            osal_printk("%s *** INPUT STREAM ***\r\n", PROBE_LOG);
        } else {
            osal_printk("%s ACK cmd=0x%02x\r\n", PROBE_LOG, data->data[2]);
        }
    }
}

/* *****************************************************************************
 * Scan / discovery callbacks — drive the chain forward
 * *****************************************************************************/

static void probe_power_on_cb(uint8_t status) {
    osal_printk("%s power on: %d\r\n", PROBE_LOG, status);
    if (status == 0) {
        errcode_t ret = enable_sle();
        if (ret != ERRCODE_SUCC) {
            osal_printk("%s enable_sle failed 0x%x\r\n", PROBE_LOG, ret);
            /* Sync failure: simulate the completion callback to keep chain alive */
            probe_sle_enable_cb((uint8_t)ret);
        }
    }
}

static void probe_sle_enable_cb(uint8_t status) {
    osal_printk("%s sle_enable_cb: %d\r\n", PROBE_LOG, status);
    if (status == 0) {
        sle_addr_t la = {0};
        if (sle_get_local_addr(&la) == ERRCODE_SUCC) {
            osal_printk("%s local addr: %02x:%02x:%02x:%02x:%02x:%02x type:%d\r\n", PROBE_LOG,
                        la.addr[0], la.addr[1], la.addr[2], la.addr[3], la.addr[4], la.addr[5],
                        la.type);
        }
        sle_setup_set_local_addr();
        probe_start_scan();
    } else {
        /* Enable failed, retry */
        osal_printk("%s sle enable failed, retry\r\n", PROBE_LOG);
        errcode_t ret = enable_sle();
        if (ret != ERRCODE_SUCC) {
            osal_printk("%s enable_sle retry failed 0x%x\r\n", PROBE_LOG, ret);
        }
    }
}

static void probe_seek_enable_cb(errcode_t status) {
    osal_printk("%s seek_enable_cb: 0x%x\r\n", PROBE_LOG, status);
}

static void probe_seek_result_cb(sle_seek_result_info_t *result) {
    if (result == NULL)
        return;
    scan_device_t *dev = scan_table_find(&result->addr);
    if (dev == NULL)
        dev = scan_table_add(&result->addr);
    if (dev == NULL)
        return;
    dev->count++;
    dev->rssi = result->rssi;
    osal_printk("%s seek: %02x:%02x:%02x:%02x:%02x:%02x rssi=%d data=", PROBE_LOG,
                result->addr.addr[0], result->addr.addr[1], result->addr.addr[2],
                result->addr.addr[3], result->addr.addr[4], result->addr.addr[5], result->rssi);
    for (uint16_t i = 0; i < result->data_length && i < 31; i++) {
        osal_printk("%02x ", result->data[i]);
    }
    osal_printk("\r\n");
    if (uapi_systick_get_ms() - g_scan_start_ms >= PROBE_SCAN_MS) {
        errcode_t ret = sle_stop_seek();
        if (ret != ERRCODE_SUCC) {
            osal_printk("%s stop_seek failed 0x%x\r\n", PROBE_LOG, ret);
        }
    }
}

static void probe_seek_disable_cb(errcode_t status) {
    osal_printk("%s seek_disable_cb: 0x%x\r\n", PROBE_LOG, status);
    if (status != ERRCODE_SUCC) {
        osal_printk("%s seek failed, rescan\r\n", PROBE_LOG);
        scan_table_reset();
        probe_start_scan();
        return;
    }
    scan_table_print();
    probe_connect_best();
}

/* *****************************************************************************
 * Connection callbacks — drive the chain forward
 * *****************************************************************************/

static void probe_connect_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
                                           sle_acb_state_t state, sle_pair_state_t pair_state,
                                           sle_disc_reason_t reason) {
    g_conn_id = conn_id;
    osal_printk("%s conn state: %d pair:%d reason:0x%x\r\n", PROBE_LOG, state, pair_state, reason);
    if (state == SLE_ACB_STATE_CONNECTED) {
        osal_printk("%s connected, conn_id=%u\r\n", PROBE_LOG, conn_id);
        /* Reset per-round state for the new connection. */
        g_disconnected = 0;
        g_discovery_done = 0;
        g_client_id = 0;
        g_find_idx = 0;
        g_service_idx = 0;
        g_sub_idx = 0;
        g_primary_cnt = 0;
        g_cur_find_type = 0xFF;
        g_notify_cnt = 0;
        g_write_cnt = 0;
        g_all_cnt = 0;
        g_cmd_cnt = 0;
        if (pair_state == SLE_PAIR_NONE) {
            const sle_addr_t *pair_addr = (addr != NULL) ? addr : &g_target_addr;
            errcode_t ret = sle_pair_remote_device(pair_addr);
            if (ret != ERRCODE_SUCC) {
                osal_printk("%s pair req failed 0x%x\r\n", PROBE_LOG, ret);
                /* Simulate pair completion with error to keep chain alive */
                probe_pair_complete_cb(conn_id, addr, ret);
            }
        } else {
            /* Already paired, proceed to exchange info */
            ssap_exchange_info_t info = {0};
            info.mtu_size = PROBE_MTU_SIZE_DEFAULT;
            info.version = 1;
            errcode_t ret = ssapc_exchange_info_req(g_client_id, g_conn_id, &info);
            if (ret != ERRCODE_SUCC) {
                osal_printk("%s exchange_info req failed 0x%x\r\n", PROBE_LOG, ret);
                ssapc_exchange_info_cb(g_client_id, g_conn_id, NULL, ret);
            }
        }
    } else if (state == SLE_ACB_STATE_DISCONNECTED) {
        osal_printk("%s disconnected, rescan\r\n", PROBE_LOG);
        g_disconnected = 1;
        scan_table_reset();
        probe_start_scan();
    }
}

static void probe_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status) {
    osal_printk("%s pair complete: c=%u conn=%u status=0x%x\r\n", PROBE_LOG, g_client_id, conn_id,
                status);
    if (status != ERRCODE_SUCC) {
        osal_printk("%s pair failed, rescan\r\n", PROBE_LOG);
        scan_table_reset();
        probe_start_scan();
        return;
    }
    ssap_exchange_info_t info = {0};
    info.mtu_size = PROBE_MTU_SIZE_DEFAULT;
    info.version = 1;
    errcode_t ret = ssapc_exchange_info_req(g_client_id, g_conn_id, &info);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s exchange_info req failed 0x%x\r\n", PROBE_LOG, ret);
        ssapc_exchange_info_cb(g_client_id, g_conn_id, NULL, ret);
    }
}

/* *****************************************************************************
 * SSAP discovery callbacks — drive the chain forward
 * *****************************************************************************/

static void ssapc_exchange_info_cb(uint8_t client_id, uint16_t conn_id, ssap_exchange_info_t *param,
                                   errcode_t status) {
    g_client_id = client_id;
    CB_CHK();
    if (status != ERRCODE_SUCC || param == NULL) {
        osal_printk("%s exchange info failed: c=%u conn=%u status=0x%x\r\n", PROBE_LOG, client_id,
                    conn_id, status);
        osal_printk("%s exchange info failed, rescan\r\n", PROBE_LOG);
        scan_table_reset();
        probe_start_scan();
        return;
    }
    osal_printk("%s exchange info: c=%u conn=%u mtu=%u\r\n", PROBE_LOG, client_id, conn_id,
                param->mtu_size);
    g_find_idx = 0;
    g_service_idx = 0;
    g_sub_idx = 0;
    g_primary_cnt = 0;
    start_next_find();
}

static void ssapc_find_structure_cb(uint8_t client_id, uint16_t conn_id,
                                    ssapc_find_service_result_t *service, errcode_t status) {
    CB_CHK();
    if (status != ERRCODE_SUCC || service == NULL)
        return;
    osal_printk("%s find_structure_cb: c=%u conn=%u status=0x%x start=0x%x "
                "end=0x%x",
                PROBE_LOG, client_id, conn_id, status, service->start_hdl, service->end_hdl);
    if (service->uuid.len > 0) {
        osal_printk(" UUID=");
        for (uint8_t i = 0; i < service->uuid.len && i < SLE_UUID_LEN; i++) {
            osal_printk("%02X", service->uuid.uuid[i]);
        }
    }
    osal_printk("\r\n");
    if (g_cur_find_type == SSAP_FIND_TYPE_PRIMARY_SERVICE && g_primary_cnt < MAX_SERVICES &&
        service->uuid.len > 0) {
        ssapc_find_service_result_t *dst = &g_primary_services[g_primary_cnt];
        dst->start_hdl = service->start_hdl;
        dst->end_hdl = service->end_hdl;
        dst->uuid.len = service->uuid.len;
        for (uint8_t i = 0; i < service->uuid.len && i < SLE_UUID_LEN; i++) {
            dst->uuid.uuid[i] = service->uuid.uuid[i];
        }
        g_primary_cnt++;
    }
}

static void ssapc_find_property_cbk(uint8_t client_id, uint16_t conn_id,
                                    ssapc_find_property_result_t *property, errcode_t status) {
    CB_CHK();
    if (status != ERRCODE_SUCC || property == NULL)
        return;
    osal_printk("%s find_property: c=%u conn=%u hdl=0x%x oper=0x%x "
                "desc_cnt=%u types=[",
                PROBE_LOG, client_id, conn_id, property->handle, property->operate_indication,
                property->descriptors_count);
    for (uint8_t i = 0; i < property->descriptors_count && i < 4; i++) {
        osal_printk("%s%02x", (i > 0) ? "," : "", property->descriptors_type[i]);
    }
    osal_printk("]\r\n");
    osal_printk("%s   prop uuid: len=%u ", PROBE_LOG, property->uuid.len);
    for (uint8_t i = 0; i < property->uuid.len && i < SLE_UUID_LEN; i++) {
        osal_printk("%02X", property->uuid.uuid[i]);
    }
    osal_printk("\r\n");

    if (g_all_cnt < 8) {
        uint8_t idx = g_all_cnt;
        g_all_hdls[idx] = property->handle;
        g_desc_cnt[idx] = (property->descriptors_count < 4) ? property->descriptors_count : 4;
        for (uint8_t i = 0; i < g_desc_cnt[idx]; i++) {
            g_desc_types[idx][i] = property->descriptors_type[i];
        }
        g_all_cnt++;
    }
    if ((property->operate_indication &
         (SSAP_OPERATE_INDICATION_BIT_WRITE | SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP)) &&
        g_write_cnt < 8) {
        g_write_hdls[g_write_cnt++] = property->handle;
    }
    if ((property->operate_indication &
         (SSAP_OPERATE_INDICATION_BIT_NOTIFY | SSAP_OPERATE_INDICATION_BIT_INDICATE)) &&
        g_notify_cnt < 8) {
        g_notify_hdls[g_notify_cnt++] = property->handle;
    }
    if ((property->operate_indication & SSAP_OPERATE_INDICATION_BIT_WRITE) && g_cmd_cnt < 8) {
        g_cmd_hdls[g_cmd_cnt++] = property->handle;
    }
}

static void ssapc_find_structure_cmp_cb(uint8_t client_id, uint16_t conn_id,
                                        ssapc_find_structure_result_t *result, errcode_t status) {
    CB_CHK();
    osal_printk("%s find_cmp: c=%u conn=%u status=0x%x type=%u\r\n", PROBE_LOG, client_id, conn_id,
                status, g_cur_find_type);
    start_next_find();
}

/* *****************************************************************************
 * SSAP data callbacks — log only
 * *****************************************************************************/

static void ssapc_write_cfm_cb(uint8_t client_id, uint16_t conn_id,
                               ssapc_write_result_t *write_result, errcode_t status) {
    CB_CHK();
    osal_printk("%s write_cfm: c=%u conn=%u status=0x%x handle=0x%x type=0x%02x\r\n", PROBE_LOG,
                client_id, conn_id, status, write_result->handle, write_result->type);
}

static void ssapc_read_cfm_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *read_data,
                              errcode_t status) {
    CB_CHK();
    if (status != ERRCODE_SUCC || read_data == NULL) {
        osal_printk("%s read_cfm: c=%u conn=%u fail status=0x%x\r\n", PROBE_LOG, client_id, conn_id,
                    status);
        return;
    }
    osal_printk("%s read_cfm: c=%u conn=%u handle=0x%x type=0x%02x len=%u ", PROBE_LOG, client_id,
                conn_id, read_data->handle, read_data->type, read_data->data_len);
    probe_print_hex(read_data->data, read_data->data_len);
}

static void ssapc_notification_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data,
                                  errcode_t status) {
    CB_CHK();
    osal_printk("%s notification: c=%u conn=%u status=0x%x ", PROBE_LOG, client_id, conn_id,
                status);
    probe_log_frame("notification", data);
}

static void ssapc_indication_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data,
                                errcode_t status) {
    CB_CHK();
    osal_printk("%s indication: c=%u conn=%u status=0x%x ", PROBE_LOG, client_id, conn_id, status);
    probe_log_frame("indication", data);
}

/* *****************************************************************************
 * Low-latency RX callback
 * *****************************************************************************/

static void low_latency_rx_cb(uint16_t len, uint8_t *value) {
    if (value == NULL || len == 0)
        return;
    osal_printk("%s low_latency_rx: len=%u ", PROBE_LOG, len);
    probe_print_hex(value, len);
}

/* *****************************************************************************
 * Experiment task — resident, waits for discovery_done each round
 * *****************************************************************************/

static int exp_task_entry(void *arg) {
    uint8_t w[17];
    ssapc_write_param_t wp;
    errcode_t ret;

    while (1) {
        /* Wait for this round's discovery to complete. */
        while (!g_discovery_done) {
            osal_msleep(100);
        }
        osal_printk("%s EXP: start, conn_id=%u\r\n", PROBE_LOG, g_conn_id);

        /* 0. Enable the low-latency RX channel AFTER the whole SSAP layer
         * is up (pair + MTU + discovery + param update done). Issuing it
         * earlier (right after pairing) made the controller disconnect
         * with reason 0x7. */
        errcode_t ll_ret = sle_low_latency_rx_enable();
        osal_printk("%s low_latency_rx_enable: 0x%x\r\n", PROBE_LOG, ll_ret);
        ll_ret = sle_low_latency_set(g_conn_id, SLE_LOW_LATENCY_ENABLE, SLE_LOW_LATENCY_1K);
        osal_printk("%s low_latency_set(1K): 0x%x\r\n", PROBE_LOG, ll_ret);

        /* 1. Read 0x13 value (baseline). */
        ret = ssapc_read_req(g_client_id, g_conn_id, 0x13, SSAP_PROPERTY_TYPE_VALUE);
        if (ret != ERRCODE_SUCC) {
            osal_printk("%s EXP: read 0x13 failed 0x%x\r\n", PROBE_LOG, ret);
        }
        for (int i = 0; i < 10 && !g_disconnected; i++) {
            osal_msleep(100);
        }

        /* 2. Subscribe notifications on 0x11 (write CCC 0x0001). */
        memset(&wp, 0, sizeof(wp));
        wp.handle = 0x11;
        wp.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
        w[0] = 0x01;
        w[1] = 0x00;
        wp.data = w;
        wp.data_len = 2;
        ret = ssapc_write_req(g_client_id, g_conn_id, &wp);
        if (ret != ERRCODE_SUCC) {
            osal_printk("%s EXP: write CCC failed 0x%x\r\n", PROBE_LOG, ret);
        }
        for (int i = 0; i < 10 && !g_disconnected; i++) {
            osal_msleep(100);
        }

        /* 3. Listen 8s (notifications logged by callbacks). */
        osal_printk("%s EXP: listening 8s\r\n", PROBE_LOG);
        for (int i = 0; i < 80 && !g_disconnected; i++) {
            osal_msleep(100);
        }
        osal_printk("%s EXP: done%s\r\n", PROBE_LOG,
                    g_disconnected ? " (aborted: disconnected)" : "");

        /* Consume the flag; wait for the next round. */
        g_discovery_done = 0;
    }
    return 0;
}

static void probe_start_exp_task(void) {
    osal_task *task = NULL;
    if (g_exp_task != NULL)
        return;
    osal_kthread_lock();
    task = osal_kthread_create(exp_task_entry, NULL, "exp_task", 4096);
    if (task != NULL) {
        osal_kthread_set_priority(task, 24);
    }
    osal_kthread_unlock();
    g_exp_task = task;
    if (task == NULL) {
        osal_printk("%s exp task create fail\r\n", PROBE_LOG);
    } else {
        osal_printk("%s exp task started\r\n", PROBE_LOG);
    }
}

/* *****************************************************************************
 * Chain drivers — each step initiates the next
 * *****************************************************************************/

static void probe_start_scan(void) {
    g_scan_start_ms = uapi_systick_get_ms();
    sle_scan_start();
}

static void probe_connect_best(void) {
    scan_device_t *best = scan_table_best();
    if (best == NULL) {
        osal_printk("%s no device found, rescan\r\n", PROBE_LOG);
        scan_table_reset();
        probe_start_scan();
        return;
    }
    memcpy_s(&g_target_addr, sizeof(sle_addr_t), &best->addr, sizeof(sle_addr_t));
    osal_printk("%s pick best: %02x:%02x:%02x:%02x:%02x:%02x rssi=%d\r\n", PROBE_LOG,
                best->addr.addr[0], best->addr.addr[1], best->addr.addr[2], best->addr.addr[3],
                best->addr.addr[4], best->addr.addr[5], best->rssi);
    errcode_t ret = sle_remove_paired_remote_device(&g_target_addr);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s remove paired failed 0x%x (non-fatal)\r\n", PROBE_LOG, ret);
    }
    ret = sle_connect_remote_device(&g_target_addr);
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s connect req failed 0x%x\r\n", PROBE_LOG, ret);
        /* Simulate disconnect to trigger rescan */
        probe_connect_state_changed_cb(g_conn_id, &g_target_addr, SLE_ACB_STATE_DISCONNECTED,
                                       SLE_PAIR_NONE, 0x0);
    }
}

static void start_next_find(void) {
    ssapc_find_structure_param_t fp = {0};
    errcode_t ret;

    /* Phase 1: top-level types. Record the type and advance the index
     * atomically BEFORE issuing the request: on success the index already
     * points at the next type (the real cmp_cb re-enters this function),
     * on rejection the loop simply tries the next one. */
    while (g_find_idx < FIND_TYPE_COUNT) {
        g_cur_find_type = g_find_types[g_find_idx];
        g_find_idx++;
        fp.type = g_cur_find_type;
        fp.start_hdl = 1;
        fp.end_hdl = 0xFFFF;
        ret = ssapc_find_structure(g_client_id, g_conn_id, &fp);
        if (ret == ERRCODE_SUCC) {
            osal_printk("%s find type=%u (top-level)\r\n", PROBE_LOG, fp.type);
            return; /* In flight, wait for the real cmp_cb */
        }
        osal_printk("%s find type=%u REJECTED err=0x%x (SDK)\r\n", PROBE_LOG, fp.type, ret);
    }

    /* Phase 2: per-service sub-element queries with UUID context. */
    while (1) {
        if (g_service_idx >= g_primary_cnt) {
            /* Mirror the official dongle: right after discovery completes,
             * switch the link to the high-rate profile. The controller may
             * stay idle until this happens. */
            sle_connection_param_update_t params = {0};
            params.conn_id = g_conn_id;
            params.interval_min = 0x64;
            params.interval_max = 0x64;
            params.max_latency = 0x3;
            params.supervision_timeout = 0x1F4;
            ret = sle_update_connect_param(&params);
            if (ret != ERRCODE_SUCC) {
                osal_printk("%s update_connect_param failed 0x%x\r\n", PROBE_LOG, ret);
            } else {
                osal_printk("%s update_connect_param: interval=0x64 latency=3 timeout=0x1F4\r\n",
                            PROBE_LOG);
            }
            osal_printk("%s discovery complete\r\n", PROBE_LOG);
            g_discovery_done = 1;
            return;
        }
        if (g_sub_idx >= SUB_TYPE_COUNT) {
            g_service_idx++;
            g_sub_idx = 0;
            continue;
        }
        ssapc_find_service_result_t *svc = &g_primary_services[g_service_idx];
        g_cur_find_type = g_sub_types[g_sub_idx];
        g_sub_idx++;
        fp.type = g_cur_find_type;
        fp.start_hdl = svc->start_hdl;
        fp.end_hdl = svc->end_hdl;
        fp.uuid = svc->uuid;
        ret = ssapc_find_structure(g_client_id, g_conn_id, &fp);
        if (ret == ERRCODE_SUCC) {
            osal_printk("%s find type=%u for svc[%u] uuid=", PROBE_LOG, fp.type, g_service_idx);
            for (uint8_t i = 0; i < svc->uuid.len && i < SLE_UUID_LEN; i++) {
                osal_printk("%02X", svc->uuid.uuid[i]);
            }
            osal_printk("\r\n");
            return; /* In flight, wait for the real cmp_cb */
        }
        osal_printk("%s find type=%u REJECTED err=0x%x (SDK)\r\n", PROBE_LOG, fp.type, ret);
    }
}

/* *****************************************************************************
 * Init
 * *****************************************************************************/

void probe_init(void) {
    g_dev_cbk.sle_power_on_cb = probe_power_on_cb;
    g_dev_cbk.sle_enable_cb = probe_sle_enable_cb;
    sle_dev_manager_register_callbacks(&g_dev_cbk);

    g_seek_cbk.seek_enable_cb = probe_seek_enable_cb;
    g_seek_cbk.seek_result_cb = probe_seek_result_cb;
    g_seek_cbk.seek_disable_cb = probe_seek_disable_cb;
    sle_announce_seek_register_callbacks(&g_seek_cbk);

    g_conn_cbk.connect_state_changed_cb = probe_connect_state_changed_cb;
    g_conn_cbk.pair_complete_cb = probe_pair_complete_cb;
    sle_connection_register_callbacks(&g_conn_cbk);

    g_ssapc_cbk.exchange_info_cb = ssapc_exchange_info_cb;
    g_ssapc_cbk.find_structure_cb = ssapc_find_structure_cb;
    g_ssapc_cbk.ssapc_find_property_cbk = ssapc_find_property_cbk;
    g_ssapc_cbk.find_structure_cmp_cb = ssapc_find_structure_cmp_cb;
    g_ssapc_cbk.write_cfm_cb = ssapc_write_cfm_cb;
    g_ssapc_cbk.read_cfm_cb = ssapc_read_cfm_cb;
    g_ssapc_cbk.notification_cb = ssapc_notification_cb;
    g_ssapc_cbk.indication_cb = ssapc_indication_cb;
    ssapc_register_callbacks(&g_ssapc_cbk);

    sle_low_latency_rx_callbacks_t ll_cbk = {0};
    ll_cbk.low_latency_rx_cb = low_latency_rx_cb;
    sle_low_latency_rx_register_callbacks(&ll_cbk);

    /* Resident experiment task; polls g_discovery_done each round. */
    probe_start_exp_task();
}
