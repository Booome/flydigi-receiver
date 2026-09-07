/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * hid_report: HID Report raw dump implementation (scenario 1).
 * No decoding — see header.
 */

#include <stdio.h>
#include <string.h>
#include "esp_hid_common.h"
#include "esp_hidh.h"
#include "hid_report.h"

void hid_report_dump_map(esp_hidh_dev_t *dev) {
    if (dev == NULL) {
        return;
    }
    size_t num_maps = 0;
    esp_hid_raw_report_map_t *maps = NULL;
    if (esp_hidh_dev_report_maps_get(dev, &num_maps, &maps) != ESP_OK || maps == NULL) {
        printf("[hid] descriptor unavailable\n");
        return;
    }
    for (size_t m = 0; m < num_maps; m++) {
        const esp_hid_raw_report_map_t *rm = &maps[m];
        printf("[hid] descriptor[%zu] len=%u bytes:\n", m, rm->len);
        for (uint16_t i = 0; i < rm->len; i++) {
            if ((i % 16) == 0) {
                printf("[hid] ");
            }
            printf("%02x ", rm->data[i]);
            if ((i % 16) == 15 || i + 1 == rm->len) {
                printf("\n");
            }
        }
    }
}

void hid_report_on_input(const uint8_t *data, uint16_t len, uint8_t report_id) {
    if (data == NULL || len == 0) {
        return;
    }
    printf("[hid] input id=%u len=%u:", report_id, len);
    for (uint16_t i = 0; i < len; i++) {
        printf(" %02x", data[i]);
    }
    printf("\n");
}
