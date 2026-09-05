/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * default: BT HID host connect + raw report capture (placeholder).
 * Real layered algorithm arrives in Task 2.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_hid_gap.h"

#define TAG "default"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_hid_gap_init(ESP_BT_MODE_BTDM));
    ESP_LOGI(TAG, "BTDM initialized (placeholder; full logic in Task 2)");

    /* Keep alive so we can verify boot in Task 1 capture */
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}