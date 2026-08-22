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

#define SLE_WAIT_MS 5000
#define SCAN_WAIT_MS 15000
#define CONNECT_WAIT_MS 10000
#define PAIR_WAIT_MS 10000
#define EXCHANGE_WAIT_MS 10000
#define FIND_WAIT_MS 5000

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

/* Event flags for task-driven flow. Callbacks set these; the task waits. */
static volatile int g_sle_enabled = 0;
static volatile int g_seek_done = 0;
static volatile int g_seek_status = 0;
static volatile int g_connected = 0;
static volatile int g_disconnected = 0;
static volatile int g_pair_done = 0;
static volatile int g_pair_status = 0;
static volatile int g_exchange_done = 0;
static volatile int g_find_done = 0;
static volatile int g_find_status = 0;

/* Pair state from connection callback. */
static sle_pair_state_t g_pair_state = SLE_PAIR_NONE;

/* Validate client_id and conn_id in SSAP callbacks. */
#define CB_CHK()                                                               \
  do {                                                                         \
    if (client_id != g_client_id || conn_id != g_conn_id) {                    \
      osal_printk("%s CB MISMATCH c=%u/%u conn=%u/%u at %s:%u\r\n", PROBE_LOG, \
                  client_id, g_client_id, conn_id, g_conn_id, __FILE__,        \
                  __LINE__);                                                   \
    }                                                                          \
  } while (0)

static osal_task *g_exp_task = NULL;

/* *****************************************************************************
 * Forward declarations
 * *****************************************************************************/

static void probe_start_exp_task(void);
static int exp_task_entry(void *arg);

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

static int wait_flag(volatile int *flag, uint32_t timeout_ms) {
  uint64_t start = uapi_systick_get_ms();
  while (*flag == 0) {
    if (g_disconnected)
      return -2;
    if (uapi_systick_get_ms() - start > timeout_ms)
      return -1;
    osal_msleep(100);
  }
  return 0;
}

static void reset_round_state(void) {
  g_sle_enabled = 0;
  g_seek_done = 0;
  g_seek_status = 0;
  g_connected = 0;
  g_disconnected = 0;
  g_pair_done = 0;
  g_pair_status = 0;
  g_exchange_done = 0;
  g_find_done = 0;
  g_find_status = 0;
  g_pair_state = SLE_PAIR_NONE;
  g_client_id = 0;
  g_conn_id = 0;
  g_find_idx = 0;
  g_service_idx = 0;
  g_sub_idx = 0;
  g_primary_cnt = 0;
  g_notify_cnt = 0;
  g_write_cnt = 0;
  g_all_cnt = 0;
  g_cmd_cnt = 0;
}

/* *****************************************************************************
 * Scan / discovery callbacks — set flags only, never drive the chain
 * *****************************************************************************/

static void probe_power_on_cb(uint8_t status) {
  osal_printk("%s power on: %d\r\n", PROBE_LOG, status);
  if (status == 0) {
    errcode_t ret = enable_sle();
    if (ret != ERRCODE_SUCC) {
      osal_printk("%s enable_sle failed 0x%x\r\n", PROBE_LOG, ret);
    }
  }
}

static void probe_sle_enable_cb(uint8_t status) {
  osal_printk("%s sle_enable_cb: %d\r\n", PROBE_LOG, status);
  if (status == 0) {
    sle_addr_t la = {0};
    if (sle_get_local_addr(&la) == ERRCODE_SUCC) {
      osal_printk("%s local addr: %02x:%02x:%02x:%02x:%02x:%02x type:%d\r\n",
                  PROBE_LOG, la.addr[0], la.addr[1], la.addr[2], la.addr[3],
                  la.addr[4], la.addr[5], la.type);
    }
    sle_setup_set_local_addr();
  }
  g_sle_enabled = 1;
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
              result->addr.addr[3], result->addr.addr[4], result->addr.addr[5],
              result->rssi);
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
  g_seek_status = status;
  g_seek_done = 1;
}

/* *****************************************************************************
 * Connection callbacks — set flags only
 * *****************************************************************************/

static void probe_connect_state_changed_cb(uint16_t conn_id,
                                           const sle_addr_t *addr,
                                           sle_acb_state_t state,
                                           sle_pair_state_t pair_state,
                                           sle_disc_reason_t reason) {
  g_conn_id = conn_id;
  osal_printk("%s conn state: %d pair:%d reason:0x%x\r\n", PROBE_LOG, state,
              pair_state, reason);
  if (state == SLE_ACB_STATE_CONNECTED) {
    osal_printk("%s connected, conn_id=%u\r\n", PROBE_LOG, conn_id);
    g_pair_state = pair_state;
    g_disconnected = 0;
    g_connected = 1;
  } else if (state == SLE_ACB_STATE_DISCONNECTED) {
    g_disconnected = 1;
  }
}

static void probe_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr,
                                   errcode_t status) {
  osal_printk("%s pair complete: c=%u conn=%u status=0x%x\r\n", PROBE_LOG,
              g_client_id, conn_id, status);
  g_pair_status = status;
  g_pair_done = 1;
}

/* *****************************************************************************
 * SSAP discovery callbacks — record data and set flags only
 * *****************************************************************************/

static void ssapc_exchange_info_cb(uint8_t client_id, uint16_t conn_id,
                                   ssap_exchange_info_t *param,
                                   errcode_t status) {
  g_client_id = client_id;
  CB_CHK();
  if (status != ERRCODE_SUCC || param == NULL) {
    osal_printk("%s exchange info failed: c=%u conn=%u status=0x%x\r\n",
                PROBE_LOG, client_id, conn_id, status);
  } else {
    osal_printk("%s exchange info: c=%u conn=%u mtu=%u\r\n", PROBE_LOG,
                client_id, conn_id, param->mtu_size);
  }
  g_exchange_done = 1;
}

static void ssapc_find_structure_cb(uint8_t client_id, uint16_t conn_id,
                                    ssapc_find_service_result_t *service,
                                    errcode_t status) {
  CB_CHK();
  if (status != ERRCODE_SUCC || service == NULL)
    return;
  osal_printk("%s find_structure_cb: c=%u conn=%u status=0x%x start=0x%x "
              "end=0x%x",
              PROBE_LOG, client_id, conn_id, status, service->start_hdl,
              service->end_hdl);
  if (service->uuid.len > 0) {
    osal_printk(" UUID=");
    for (uint8_t i = 0; i < service->uuid.len && i < SLE_UUID_LEN; i++) {
      osal_printk("%02X", service->uuid.uuid[i]);
    }
  }
  osal_printk("\r\n");
  if (g_find_types[g_find_idx] == SSAP_FIND_TYPE_PRIMARY_SERVICE &&
      g_primary_cnt < MAX_SERVICES && service->uuid.len > 0) {
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
                                    ssapc_find_property_result_t *property,
                                    errcode_t status) {
  CB_CHK();
  if (status != ERRCODE_SUCC || property == NULL)
    return;
  osal_printk("%s find_property: c=%u conn=%u hdl=0x%x oper=0x%x "
              "desc_cnt=%u types=[",
              PROBE_LOG, client_id, conn_id, property->handle,
              property->operate_indication, property->descriptors_count);
  for (uint8_t i = 0; i < property->descriptors_count && i < 4; i++) {
    osal_printk("%s%02x", (i > 0) ? "," : "", property->descriptors_type[i]);
  }
  osal_printk("]\r\n");

  if (g_all_cnt < 8) {
    uint8_t idx = g_all_cnt;
    g_all_hdls[idx] = property->handle;
    g_desc_cnt[idx] =
        (property->descriptors_count < 4) ? property->descriptors_count : 4;
    for (uint8_t i = 0; i < g_desc_cnt[idx]; i++) {
      g_desc_types[idx][i] = property->descriptors_type[i];
    }
    g_all_cnt++;
  }
  if ((property->operate_indication &
       (SSAP_OPERATE_INDICATION_BIT_WRITE |
        SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP)) &&
      g_write_cnt < 8) {
    g_write_hdls[g_write_cnt++] = property->handle;
  }
  if ((property->operate_indication & (SSAP_OPERATE_INDICATION_BIT_NOTIFY |
                                       SSAP_OPERATE_INDICATION_BIT_INDICATE)) &&
      g_notify_cnt < 8) {
    g_notify_hdls[g_notify_cnt++] = property->handle;
  }
  if ((property->operate_indication & SSAP_OPERATE_INDICATION_BIT_WRITE) &&
      g_cmd_cnt < 8) {
    g_cmd_hdls[g_cmd_cnt++] = property->handle;
  }
}

static void ssapc_find_structure_cmp_cb(uint8_t client_id, uint16_t conn_id,
                                        ssapc_find_structure_result_t *result,
                                        errcode_t status) {
  CB_CHK();
  osal_printk("%s find_cmp: c=%u conn=%u status=0x%x\r\n", PROBE_LOG, client_id,
              conn_id, status);
  g_find_status = status;
  g_find_done = 1;
}

/* *****************************************************************************
 * SSAP data callbacks — log only
 * *****************************************************************************/

static void ssapc_write_cfm_cb(uint8_t client_id, uint16_t conn_id,
                               ssapc_write_result_t *write_result,
                               errcode_t status) {
  CB_CHK();
  osal_printk(
      "%s write_cfm: c=%u conn=%u status=0x%x handle=0x%x type=0x%02x\r\n",
      PROBE_LOG, client_id, conn_id, status, write_result->handle,
      write_result->type);
}

static void ssapc_read_cfm_cb(uint8_t client_id, uint16_t conn_id,
                              ssapc_handle_value_t *read_data,
                              errcode_t status) {
  CB_CHK();
  if (status != ERRCODE_SUCC || read_data == NULL) {
    osal_printk("%s read_cfm: c=%u conn=%u fail status=0x%x\r\n", PROBE_LOG,
                client_id, conn_id, status);
    return;
  }
  osal_printk("%s read_cfm: c=%u conn=%u handle=0x%x type=0x%02x len=%u ",
              PROBE_LOG, client_id, conn_id, read_data->handle, read_data->type,
              read_data->data_len);
  probe_print_hex(read_data->data, read_data->data_len);
}

static void ssapc_notification_cb(uint8_t client_id, uint16_t conn_id,
                                  ssapc_handle_value_t *data,
                                  errcode_t status) {
  CB_CHK();
  osal_printk("%s notification: c=%u conn=%u status=0x%x ", PROBE_LOG,
              client_id, conn_id, status);
  probe_log_frame("notification", data);
}

static void ssapc_indication_cb(uint8_t client_id, uint16_t conn_id,
                                ssapc_handle_value_t *data, errcode_t status) {
  CB_CHK();
  osal_printk("%s indication: c=%u conn=%u status=0x%x ", PROBE_LOG, client_id,
              conn_id, status);
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
 * Experiment task — the sole driver of the discovery + experiment flow
 * *****************************************************************************/

static int exp_task_entry(void *arg) {
  uint8_t w[17];
  ssapc_write_param_t wp;
  errcode_t ret;

  while (1) {
    reset_round_state();

    /* 1. Wait for SLE enabled (driven by power_on_cb). */
    osal_printk("%s TASK: waiting for SLE\r\n", PROBE_LOG);
    if (wait_flag(&g_sle_enabled, SLE_WAIT_MS) != 0) {
      osal_printk("%s TASK: SLE enable timeout, retry\r\n", PROBE_LOG);
      osal_msleep(1000);
      continue;
    }

    /* 2. Scan for devices. */
    osal_printk("%s TASK: starting scan\r\n", PROBE_LOG);
    scan_table_reset();
    g_scan_start_ms = uapi_systick_get_ms();
    sle_scan_start();
    if (wait_flag(&g_seek_done, SCAN_WAIT_MS) != 0) {
      osal_printk("%s TASK: scan timeout, retry\r\n", PROBE_LOG);
      continue;
    }
    if (g_seek_status != ERRCODE_SUCC) {
      osal_printk("%s TASK: seek failed 0x%x, retry\r\n", PROBE_LOG,
                  g_seek_status);
      continue;
    }

    /* 3. Connect to best device. */
    scan_device_t *best = scan_table_best();
    if (best == NULL) {
      osal_printk("%s TASK: no device, retry\r\n", PROBE_LOG);
      continue;
    }
    memcpy_s(&g_target_addr, sizeof(sle_addr_t), &best->addr,
             sizeof(sle_addr_t));
    osal_printk("%s TASK: connecting %02x:%02x:%02x:%02x:%02x:%02x rssi=%d\r\n",
                PROBE_LOG, best->addr.addr[0], best->addr.addr[1],
                best->addr.addr[2], best->addr.addr[3], best->addr.addr[4],
                best->addr.addr[5], best->rssi);
    sle_remove_paired_remote_device(&g_target_addr);
    ret = sle_connect_remote_device(&g_target_addr);
    if (ret != ERRCODE_SUCC) {
      osal_printk("%s TASK: connect req failed 0x%x, retry\r\n", PROBE_LOG,
                  ret);
      continue;
    }
    if (wait_flag(&g_connected, CONNECT_WAIT_MS) != 0) {
      osal_printk("%s TASK: connect timeout, retry\r\n", PROBE_LOG);
      continue;
    }

    /* 4. Pair if needed. */
    if (g_pair_state == SLE_PAIR_NONE) {
      osal_printk("%s TASK: pairing\r\n", PROBE_LOG);
      ret = sle_pair_remote_device(&g_target_addr);
      if (ret != ERRCODE_SUCC) {
        osal_printk("%s TASK: pair req failed 0x%x, retry\r\n", PROBE_LOG, ret);
        continue;
      }
      if (wait_flag(&g_pair_done, PAIR_WAIT_MS) != 0) {
        osal_printk("%s TASK: pair timeout, retry\r\n", PROBE_LOG);
        continue;
      }
      if (g_pair_status != ERRCODE_SUCC) {
        osal_printk("%s TASK: pair failed 0x%x, retry\r\n", PROBE_LOG,
                    g_pair_status);
        continue;
      }
    } else {
      osal_printk("%s TASK: already paired, skip pair\r\n", PROBE_LOG);
    }

    /* 5. Exchange info. */
    osal_printk("%s TASK: exchange info\r\n", PROBE_LOG);
    ssap_exchange_info_t info = {0};
    info.mtu_size = PROBE_MTU_SIZE_DEFAULT;
    info.version = 1;
    ret = ssapc_exchange_info_req(g_client_id, g_conn_id, &info);
    if (ret != ERRCODE_SUCC) {
      osal_printk("%s TASK: exchange_info req failed 0x%x, retry\r\n",
                  PROBE_LOG, ret);
      continue;
    }
    if (wait_flag(&g_exchange_done, EXCHANGE_WAIT_MS) != 0) {
      osal_printk("%s TASK: exchange timeout, retry\r\n", PROBE_LOG);
      continue;
    }

    /* 6. Discovery loop — top-level types. */
    osal_printk("%s TASK: discovery loop start\r\n", PROBE_LOG);
    g_find_idx = 0;
    g_service_idx = 0;
    g_sub_idx = 0;
    g_primary_cnt = 0;

    for (; g_find_idx < FIND_TYPE_COUNT; g_find_idx++) {
      ssapc_find_structure_param_t fp = {0};
      fp.type = g_find_types[g_find_idx];
      fp.start_hdl = 1;
      fp.end_hdl = 0xFFFF;
      osal_printk("%s TASK: find type=%u (top-level)\r\n", PROBE_LOG, fp.type);
      ret = ssapc_find_structure(g_client_id, g_conn_id, &fp);
      if (ret != ERRCODE_SUCC) {
        osal_printk("%s TASK: find type=%u REJECTED err=0x%x (SDK)\r\n",
                    PROBE_LOG, fp.type, ret);
        continue;
      }
      if (wait_flag(&g_find_done, FIND_WAIT_MS) != 0) {
        osal_printk("%s TASK: find type=%u TIMEOUT\r\n", PROBE_LOG, fp.type);
        break;
      }
      g_find_done = 0;
    }

    /* 6b. Per-service sub-element types. */
    for (g_service_idx = 0; g_service_idx < g_primary_cnt; g_service_idx++) {
      for (g_sub_idx = 0; g_sub_idx < SUB_TYPE_COUNT; g_sub_idx++) {
        ssapc_find_structure_param_t fp = {0};
        fp.type = g_sub_types[g_sub_idx];
        ssapc_find_service_result_t *svc = &g_primary_services[g_service_idx];
        fp.start_hdl = svc->start_hdl;
        fp.end_hdl = svc->end_hdl;
        fp.uuid = svc->uuid;
        osal_printk("%s TASK: find type=%u for svc[%u] uuid=", PROBE_LOG,
                    fp.type, g_service_idx);
        for (uint8_t i = 0; i < svc->uuid.len && i < SLE_UUID_LEN; i++) {
          osal_printk("%02X", svc->uuid.uuid[i]);
        }
        osal_printk("\r\n");
        ret = ssapc_find_structure(g_client_id, g_conn_id, &fp);
        if (ret != ERRCODE_SUCC) {
          osal_printk("%s TASK: find type=%u REJECTED err=0x%x (SDK)\r\n",
                      PROBE_LOG, fp.type, ret);
          continue;
        }
        if (wait_flag(&g_find_done, FIND_WAIT_MS) != 0) {
          osal_printk("%s TASK: find type=%u TIMEOUT\r\n", PROBE_LOG, fp.type);
          break;
        }
        g_find_done = 0;
      }
    }

    osal_printk("%s TASK: discovery complete\r\n", PROBE_LOG);

    /* 7. Experiment. */
    osal_printk("%s EXP: read 0x13\r\n", PROBE_LOG);
    ret =
        ssapc_read_req(g_client_id, g_conn_id, 0x13, SSAP_PROPERTY_TYPE_VALUE);
    if (ret != ERRCODE_SUCC) {
      osal_printk("%s EXP: read 0x13 failed 0x%x\r\n", PROBE_LOG, ret);
    }
    osal_msleep(1000);

    osal_printk("%s EXP: enable notify on 0x11\r\n", PROBE_LOG);
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
    osal_msleep(1000);

    osal_printk("%s EXP: listening 8s\r\n", PROBE_LOG);
    osal_msleep(8000);

    osal_printk("%s EXP: done\r\n", PROBE_LOG);

    /* Small delay before next round. */
    osal_msleep(1000);
  }

  return 0;
}

static void probe_start_exp_task(void) {
  if (g_exp_task != NULL)
    return;
  osal_kthread_lock();
  g_exp_task = osal_kthread_create(exp_task_entry, NULL, "exp_task", 4096);
  if (g_exp_task != NULL) {
    osal_kthread_set_priority(g_exp_task, 24);
  }
  osal_kthread_unlock();
  if (g_exp_task == NULL) {
    osal_printk("%s exp task create fail\r\n", PROBE_LOG);
  } else {
    osal_printk("%s exp task started\r\n", PROBE_LOG);
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

  probe_start_exp_task();
}
