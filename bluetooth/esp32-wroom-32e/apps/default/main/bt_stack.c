/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Minimal BR/EDR (Bluedroid BTDM) bring-up, distilled from the ESP-IDF esp_hid
 * example glue: controller + Bluedroid enable, SSP NoInputNoOutput (headless
 * Just Works), legacy pin, connectable+non-discoverable scan mode. The GAP
 * callback and discovery are the caller's (main.c) responsibility.
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "bt_stack.h"

static const char *TAG = "bt_stack";

esp_err_t bt_stack_start(void) {
    esp_err_t ret;

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_BTDM;
    bt_cfg.bt_max_acl_conn = 3;
    bt_cfg.bt_max_sync_conn = 3;

    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "controller init failed: %d", ret);
        return ret;
    }
    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM)) != ESP_OK) {
        ESP_LOGE(TAG, "controller enable failed: %d", ret);
        return ret;
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid init failed: %d", ret);
        return ret;
    }
    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid enable failed: %d", ret);
        return ret;
    }

    /* Headless host: advertise NoInputNoOutput so SSP uses Just Works and a
     * reconnect never waits on a passkey/display the user cannot provide. */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    /* Allow bonded peers to page us back; we are not ourselves discoverable. */
    if ((ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE)) != ESP_OK) {
        ESP_LOGE(TAG, "set_scan_mode failed: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "BR/EDR (BTDM) ready");
    return ESP_OK;
}
