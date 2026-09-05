/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * bt_scan: continuous BR/EDR + BLE dual-mode scan (M10).
 * Adapted from ESP-IDF examples/bluetooth/esp_hid_host; auto-connect
 * and state machine stripped; only esp_hid_scan loop + printf remain.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_hid_gap.h"

#define TAG "bt_scan"
#define SCAN_DURATION_SEC 5
#define SCAN_GAP_MS 500

static void print_scan_results(esp_hid_scan_result_t *results, size_t n) {
    for (size_t i = 0; i < n; i++) {
        esp_hid_scan_result_t *r = &results[i];
        const char *mode = "?";
        unsigned int major = 0, minor = 0, service = 0, appearance = 0;
        const char *name = (r->name && r->name[0]) ? r->name : "";

        if (r->transport == ESP_HID_TRANSPORT_BLE) {
            mode = "BLE";
            appearance = r->ble.appearance;
        } else if (r->transport == ESP_HID_TRANSPORT_BT) {
            mode = "BR_EDR";
            major = r->bt.cod.major;
            minor = r->bt.cod.minor;
            service = r->bt.cod.service;
        } else {
            mode = "USB"; /* not expected on ESP32 */
        }

        printf("[bt_scan] mode=%-5s addr=%02x:%02x:%02x:%02x:%02x:%02x "
               "name=\"%s\"",
               mode, r->bda[0], r->bda[1], r->bda[2], r->bda[3], r->bda[4], r->bda[5], name);

        if (r->transport == ESP_HID_TRANSPORT_BT) {
            printf(" cod(major=%u minor=%u srv=0x%03x)", major, minor, service);
        } else if (r->transport == ESP_HID_TRANSPORT_BLE) {
            printf(" appearance=0x%04x", appearance);
        }
        printf(" rssi=%d\n", r->rssi);
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_hid_gap_init(ESP_BT_MODE_BTDM));
    ESP_LOGI(TAG, "ESP_HID_GAP initialized in dual mode (BR/EDR + BLE)");

    while (1) {
        esp_hid_scan_result_t *results = NULL;
        size_t num_results = 0;

        esp_err_t scan_ret = esp_hid_scan(SCAN_DURATION_SEC, &num_results, &results);
        if (scan_ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_hid_scan failed: %s", esp_err_to_name(scan_ret));
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (num_results > 0) {
            print_scan_results(results, num_results);
        }

        esp_hid_scan_results_free(results);
        vTaskDelay(pdMS_TO_TICKS(SCAN_GAP_MS));
    }
}