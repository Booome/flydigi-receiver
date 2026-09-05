/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * HID Report Descriptor dump + (later) input-report decode for the Apex5
 * BT path. See spec 2026-09-05-bt-hid-report-decode.
 */

#include "hid_report.h"
#include <stdio.h>
#include "esp_hid_common.h"
#include "esp_log.h"

static void dump_hex(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        if (i % 16 == 0) {
            printf("\n[hid]  ");
        }
        printf("%02x ", data[i]);
    }
    printf("\n");
}

void hid_dump_report_map(esp_hidh_dev_t *dev) {
    size_t n_maps = 0;
    esp_hid_raw_report_map_t *maps = NULL;
    if (esp_hidh_dev_report_maps_get(dev, &n_maps, &maps) != ESP_OK) {
        ESP_LOGE("hid_report", "report_maps_get failed");
        return;
    }
    for (size_t m = 0; m < n_maps; m++) {
        printf("[hid] descriptor[%zu] len=%u bytes:", m, maps[m].len);
        dump_hex(maps[m].data, maps[m].len);

        esp_hid_report_map_t *parsed = esp_hid_parse_report_map(maps[m].data, maps[m].len);
        if (!parsed) {
            ESP_LOGE("hid_report", "parse_report_map failed");
            continue;
        }
        for (int i = 0; i < parsed->reports_len; i++) {
            esp_hid_report_item_t *r = &parsed->reports[i];
            printf(
                "[hid] report: map=%u id=%u type=%s usage=%s len=%u\n",
                r->map_index,
                r->report_id,
                esp_hid_report_type_str(r->report_type),
                esp_hid_usage_str(r->usage),
                r->value_len
            );
        }
        esp_hid_free_report_map(parsed);
    }
}