/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 * SPDX-License-Identifier: CC0-1.0
 *
 * Passive reconnect test: BTDM up, esp_hidh up, CONNECTABLE only, no
 * dev_open, no timer, no candidate. Observe whether the stack's bonded-peer
 * auto-reconnect reaches OPEN_EVENT. Compare against scenario 2 capture
 * 426219 where app-side lock_tick interfered.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh.h"

static const char *bda_str(const uint8_t *bda) {
    static char buf[18];
    snprintf(
        buf,
        sizeof(buf),
        "%02x:%02x:%02x:%02x:%02x:%02x",
        bda[0],
        bda[1],
        bda[2],
        bda[3],
        bda[4],
        bda[5]
    );
    return buf;
}

static void hidh_cb(void *arg, esp_event_base_t base, int32_t id, void *data) {
    esp_hidh_event_data_t *p = data;
    switch (id) {
    case ESP_HIDH_OPEN_EVENT: {
        const uint8_t *bda = p->open.dev ? esp_hidh_dev_bda_get(p->open.dev) : NULL;
        printf(
            "[passive] OPEN status=%d addr=%s\n", p->open.status, bda ? bda_str(bda) : "(no dev)"
        );
        break;
    }
    case ESP_HIDH_CLOSE_EVENT: {
        const uint8_t *bda = p->close.dev ? esp_hidh_dev_bda_get(p->close.dev) : NULL;
        printf(
            "[passive] CLOSE status=%d reason=%d addr=%s\n",
            p->close.status,
            p->close.reason,
            bda ? bda_str(bda) : "(no dev)"
        );
        break;
    }
    case ESP_HIDH_INPUT_EVENT: {
        printf("[passive] INPUT len=%u id=%u", (unsigned)p->input.length, p->input.report_id);
        for (size_t i = 0; i < p->input.length; i++) {
            printf(" %02x", p->input.data[i]);
        }
        printf("\n");
        break;
    }
    default:
        printf("[passive] hidh evt=%ld\n", (long)id);
        break;
    }
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        printf(
            "[passive] ACL_CONN hdl=0x%x st=%d addr=%s\n",
            param->acl_conn_cmpl_stat.handle,
            param->acl_conn_cmpl_stat.stat,
            bda_str(param->acl_conn_cmpl_stat.bda)
        );
        break;
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        printf(
            "[passive] ACL_DISCONN hdl=0x%x reason=0x%x\n",
            param->acl_disconn_cmpl_stat.handle,
            param->acl_disconn_cmpl_stat.reason
        );
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        printf(
            "[passive] AUTH stat=%d lk=%d addr=%s\n",
            param->auth_cmpl.stat,
            param->auth_cmpl.lk_type,
            bda_str(param->auth_cmpl.bda)
        );
        break;
    default:
        break;
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_BTDM;
    bt_cfg.bt_max_acl_conn = 3;
    bt_cfg.bt_max_sync_conn = 3;
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&bluedroid_cfg));
    ESP_ERROR_CHECK(esp_bluedroid_enable());

#if CONFIG_BT_BLE_ENABLED
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler));
#endif

    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap)));
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE));

    esp_hidh_config_t hidh_cfg = {.callback = hidh_cb};
    ESP_ERROR_CHECK(esp_hidh_init(&hidh_cfg));

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));

    printf(
        "[passive] boot bonds=%d (passive — no dev_open, no timer)\n",
        esp_bt_gap_get_bond_device_num()
    );
}