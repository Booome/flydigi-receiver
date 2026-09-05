#include "bs21_util.h"
#include "chip_io.h"
#include "decoy_server.h"
#include "errcode.h"
#include "nv.h"
#include "securec.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_device_manager.h"
#include "soc_osal.h"

#define DECOY_LOG "[decoy]"
#define ADV_HANDLE 1
#define ADV_DATA_LEN_MAX 251
#define ADV_INTERVAL_MIN 0xC8
#define ADV_INTERVAL_MAX 0xC8
#define CONN_INTV_MIN 0x64
#define CONN_INTV_MAX 0x64
#define CONN_MAX_LATENCY 0x1F3
#define CONN_SUPERVISION_TO 0x64

#define ADV_CHANNEL_MAP_DEFAULT 0x07
#define ADV_DATA_TYPE_DISCOVERY_LEVEL 0x01
#define ADV_DATA_TYPE_ACCESS_MODE 0x02
#define ADV_DATA_TYPE_TX_POWER_LEVEL 0x0C
#define ADV_TX_POWER 10

/* Spoof the real controller address so the dongle's binding matches.
 * Power off the real controller during the test to avoid conflicts. */
#define USE_SPOOFED_ADDR 1

#if USE_SPOOFED_ADDR
/* Real controller address (from seek logs). */
static unsigned char g_local_addr[6] = {0xA1, 0xA2, 0xC8, 0x75, 0x43, 0xB8};
#else
static unsigned char g_local_addr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
#endif

/* Captured over the air from the real controller:
 * announce: 01 01 01 05 04 0b 06 09 06 03 12 09 06 07 03 02 05 00 06 0a "fly_digig1"
 * rsp:      0b 0a "fly_digiggs" */
static const uint8_t g_adv_data[] = {0x01, 0x01, 0x01, 0x05, 0x04, 0x0B, 0x06, 0x09, 0x06, 0x03,
                                     0x12, 0x09, 0x06, 0x07, 0x03, 0x02, 0x05, 0x00, 0x06, 0x0A,
                                     0x66, 0x6C, 0x79, 0x5F, 0x64, 0x69, 0x67, 0x69, 0x67, 0x31};
static const uint8_t g_adv_rsp[] = {
    0x0B, 0x0A, 0x66, 0x6C, 0x79, 0x5F, 0x64, 0x69, 0x67, 0x69, 0x67, 0x73
};

static sle_dev_manager_callbacks_t g_dev_cbk = {0};
static sle_announce_seek_callbacks_t g_seek_cbk = {0};
static sle_connection_callbacks_t g_conn_cbk = {0};

static int set_announce_param(void) {
    sle_announce_param_t param = {0};

    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = ADV_HANDLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = ADV_CHANNEL_MAP_DEFAULT;
    param.announce_interval_min = ADV_INTERVAL_MIN;
    param.announce_interval_max = ADV_INTERVAL_MAX;
    param.conn_interval_min = CONN_INTV_MIN;
    param.conn_interval_max = CONN_INTV_MAX;
    param.conn_max_latency = CONN_MAX_LATENCY;
    param.conn_supervision_timeout = CONN_SUPERVISION_TO;
    param.own_addr.type = 0;
    if (memcpy_s(param.own_addr.addr, 6, g_local_addr, 6) != EOK) {
        return -1;
    }
    return sle_set_announce_param(param.announce_handle, &param);
}

static int set_announce_data(void) {
    sle_announce_data_t data = {0};
    data.announce_data = (uint8_t *)g_adv_data;
    data.announce_data_len = sizeof(g_adv_data);
    data.seek_rsp_data = (uint8_t *)g_adv_rsp;
    data.seek_rsp_data_len = sizeof(g_adv_rsp);
    return sle_set_announce_data(ADV_HANDLE, &data);
}

static void announce_enable_cb(uint32_t announce_id, errcode_t status) {
    osal_printk("%s announce enable id:%u status:0x%x\r\n", DECOY_LOG, announce_id, status);
}

static void sle_power_on_cb(uint8_t status) {
    osal_printk("%s power on: %d\r\n", DECOY_LOG, status);
    enable_sle();
}

static void sle_enable_cb(uint8_t status) {
    errcode_t rc;
    osal_printk("%s sle enable: %d\r\n", DECOY_LOG, status);
    if (status != 0) {
        return;
    }

    /* Official flow: set_info + service registration from the enable
     * callback; SSAP callbacks were registered before enable_sle(). */
    decoy_services_add();

    sle_addr_t la = {0};
    if (sle_get_local_addr(&la) == ERRCODE_SUCC) {
        osal_printk(
            "%s local addr: %02x:%02x:%02x:%02x:%02x:%02x\r\n",
            DECOY_LOG,
            la.addr[0],
            la.addr[1],
            la.addr[2],
            la.addr[3],
            la.addr[4],
            la.addr[5]
        );
    }
    sle_setup_set_local_addr();

    sle_announce_seek_register_callbacks(&g_seek_cbk);
    rc = set_announce_param();
    if (rc != 0) {
        osal_printk("%s set_announce_param fail %d\r\n", DECOY_LOG, rc);
        return;
    }
    rc = set_announce_data();
    if (rc != 0) {
        osal_printk("%s set_announce_data fail %d\r\n", DECOY_LOG, rc);
        return;
    }
    rc = sle_start_announce(ADV_HANDLE);
    osal_printk("%s start announce: 0x%x\r\n", DECOY_LOG, rc);
}

static void *re_announce_task(const char *arg) {
    osal_msleep(5000);
    errcode_t rc = sle_start_announce(ADV_HANDLE);
    osal_printk("%s re-announce rc:0x%x\r\n", DECOY_LOG, rc);
    return NULL;
}

static void conn_state_changed_cb(
    uint16_t conn_id,
    const sle_addr_t *addr,
    sle_acb_state_t conn_state,
    sle_pair_state_t pair_state,
    sle_disc_reason_t disc_reason
) {
    osal_printk(
        "%s conn id:%u state:%d pair:%d disc:0x%x\r\n",
        DECOY_LOG,
        conn_id,
        conn_state,
        pair_state,
        disc_reason
    );
    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        osal_printk("%s *** dongle connected ***\r\n", DECOY_LOG);
        decoy_mark_connected();
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        if (disc_reason != SLE_DISCONNECT_BY_LOCAL) {
            osal_task *t =
                osal_kthread_create((osal_kthread_handler)re_announce_task, 0, "reann", 0x1000);
            if (t != NULL) {
                osal_kfree(t);
            }
        }
    }
}

static void decoy_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status) {
    osal_printk("%s pair complete: conn=%u status=0x%x\r\n", DECOY_LOG, conn_id, status);
    if (status == ERRCODE_SUCC) {
        decoy_mark_pair_complete();
    }
}

static void decoy_param_update_req_cb(
    uint16_t conn_id, errcode_t status, const sle_connection_param_update_req_t *param
) {
    osal_printk(
        "%s param update REQ: conn=%u status=0x%x min=%u max=%u lat=%u to=%u\r\n",
        DECOY_LOG,
        conn_id,
        status,
        param->interval_min,
        param->interval_max,
        param->max_latency,
        param->supervision_timeout
    );
}

static void decoy_param_update_cb(
    uint16_t conn_id, errcode_t status, const sle_connection_param_update_evt_t *param
) {
    osal_printk(
        "%s param update: conn=%u status=0x%x interval=%u\r\n",
        DECOY_LOG,
        conn_id,
        status,
        param->interval
    );
}

static void auth_complete_cb(
    uint16_t conn_id, const sle_addr_t *addr, errcode_t status, const sle_auth_info_evt_t *evt
) {
    osal_printk("%s auth complete id:%u status:0x%x\r\n", DECOY_LOG, conn_id, status);
    if (status != ERRCODE_SUCC || evt == NULL) {
        return;
    }
    sle_addr_t own_addr = {0};
    if (sle_get_local_addr(&own_addr) != ERRCODE_SUCC) {
        return;
    }
    errcode_t nv_ret =
        sle_set_nv_smp_keys((sle_auth_info_evt_t *)evt, &own_addr, (sle_addr_t *)addr, 0);
    if (nv_ret != ERRCODE_SUCC) {
        osal_printk("%s save smp keys fail: 0x%x\r\n", DECOY_LOG, nv_ret);
    }
}

void axk_main(void) {
    bs21_rst();
    osal_printk("app: flydigi_decoy\r\n");

    /* NV must be initialised before pairing keys can be stored; without it
     * the dongle's encrypted link fails right after MTU exchange (disc 0x7). */
    uapi_nv_init();

    g_dev_cbk.sle_power_on_cb = sle_power_on_cb;
    g_dev_cbk.sle_enable_cb = sle_enable_cb;
    sle_dev_manager_register_callbacks(&g_dev_cbk);

    g_seek_cbk.announce_enable_cb = announce_enable_cb;
    g_conn_cbk.connect_state_changed_cb = conn_state_changed_cb;
    g_conn_cbk.connect_param_update_req_cb = decoy_param_update_req_cb;
    g_conn_cbk.connect_param_update_cb = decoy_param_update_cb;
    g_conn_cbk.pair_complete_cb = decoy_pair_complete_cb;
    g_conn_cbk.auth_complete_cb = auth_complete_cb;
    sle_connection_register_callbacks(&g_conn_cbk);

    /* Official flow: SSAP + low-latency callbacks registered BEFORE
     * enable_sle(). */
    decoy_server_early_init();
    decoy_low_latency_init();

    enable_sle();
}
