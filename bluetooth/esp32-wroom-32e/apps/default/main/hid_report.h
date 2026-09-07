/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * hid_report: HID Report tool module (scenario 1 — raw dump only).
 *
 * Scenario 1 scope: when an INPUT report arrives, print the raw bytes to UART
 * (no decoding). Decoding is deferred until we have measured button/stick/trigger
 * semantics on hardware.
 *
 * The descriptor dump on OPEN prints the raw HID Report Descriptor bytes for
 * offline analysis.
 */

#ifndef BT_APP_HID_REPORT_H
#define BT_APP_HID_REPORT_H

#include <stdint.h>
#include <stddef.h>
#include "esp_hidh.h"

#ifdef __cplusplus
extern "C" {
#endif

void hid_report_dump_map(esp_hidh_dev_t *dev);

void hid_report_on_input(const uint8_t *data, uint16_t len, uint8_t report_id);

#ifdef __cplusplus
}
#endif

#endif /* BT_APP_HID_REPORT_H */