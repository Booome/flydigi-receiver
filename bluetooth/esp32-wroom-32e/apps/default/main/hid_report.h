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

/* Button bit masks in apex5_xinput_t.buttons. HYPOTHESIS from the public
 * Xbox-360 15-byte input layout; Task 3 must confirm/correct against the
 * live descriptor + per-key measurement, NOT treat these as given. */
#define BTN_DPAD_UP 0x0001
#define BTN_DPAD_DOWN 0x0002
#define BTN_DPAD_LEFT 0x0004
#define BTN_DPAD_RIGHT 0x0008
#define BTN_BACK 0x0010
#define BTN_START 0x0020
#define BTN_L3 0x0040
#define BTN_R3 0x0080
#define BTN_LB 0x0100
#define BTN_RB 0x0200
#define BTN_GUIDE 0x0400
#define BTN_A 0x1000
#define BTN_B 0x2000
#define BTN_X 0x4000
#define BTN_Y 0x8000

typedef struct {
    uint32_t buttons;      /* bitfield of BTN_* */
    uint8_t left_trigger;  /* 0..255 hypothesis */
    uint8_t right_trigger; /* 0..255 hypothesis */
    int16_t lx, ly;        /* left stick, signed, LE hypothesis */
    int16_t rx, ry;        /* right stick, signed, LE hypothesis */
} apex5_xinput_t;

/* Debug byte-deltas for layout bring-up (Task 3). Single definition here so
 * both main.c and hid_report.c share it. Set 0 to disable (Task 3 end). */
#define HID_DEBUG_DELTA 1

bool hid_decode(const uint8_t *buf, uint16_t len, apex5_xinput_t *out);
void hid_print_state(const apex5_xinput_t *cur);

#endif /* BT_APP_HID_REPORT_H */
