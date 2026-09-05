/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#ifndef BT_APP_HID_REPORT_H
#define BT_APP_HID_REPORT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_hidh.h"

/* Dump + parse the device's HID Report Descriptor once, on connect.
 * Prints raw map bytes (16B/line) and per-report metadata via
 * esp_hid_parse_report_map (id/type/usage/len). */
void hid_dump_report_map(esp_hidh_dev_t *dev);

#endif /* BT_APP_HID_REPORT_H */