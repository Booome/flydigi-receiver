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
bool hid_decode(const uint8_t *buf, uint16_t len, apex5_xinput_t *out) {
    /* HYPOTHESIS layout (Xbox-360 15-byte, LE), corrected in Task 3:
     *   [0]      report id
     *   [1..2]   buttons (16-bit LE)  -> low half of out->buttons
     *   [4]      left trigger
     *   [5]      right trigger
     *   [6..7]   lx  [8..9] ly  [10..11] rx  [12..13] ry  (int16 LE)
     */
    if (len < 15) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->buttons = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8); /* buttons half-word */
    out->left_trigger = buf[4];
    out->right_trigger = buf[5];
    out->lx = (int16_t)(buf[6] | (buf[7] << 8));
    out->ly = (int16_t)(buf[8] | (buf[9] << 8));
    out->rx = (int16_t)(buf[10] | (buf[11] << 8));
    out->ry = (int16_t)(buf[12] | (buf[13] << 8));
    return true;
}

static void print_buttons(uint32_t b, char *buf, size_t n) {
    buf[0] = '\0';
    struct {
        uint32_t bit;
        const char *name;
    } t[] = {
        {BTN_A, "A"},
        {BTN_B, "B"},
        {BTN_X, "X"},
        {BTN_Y, "Y"},
        {BTN_LB, "LB"},
        {BTN_RB, "RB"},
        {BTN_BACK, "Back"},
        {BTN_START, "Start"},
        {BTN_GUIDE, "Guide"},
        {BTN_L3, "L3"},
        {BTN_R3, "R3"},
    };
    size_t used = 0;
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        if (b & t[i].bit) {
            int w = snprintf(buf + used, n - used, used ? "|%s" : "%s", t[i].name);
            if (w > 0)
                used += (size_t)w;
        }
    }
    if (used == 0) {
        snprintf(buf, n, "-");
    }
}

void hid_print_state(const apex5_xinput_t *cur) {
    char btn[64];
    print_buttons(cur->buttons, btn, sizeof(btn));
    static char d[8];
    d[0] = '\0';
    if (cur->buttons & BTN_DPAD_UP)
        strcat(d, "U");
    if (cur->buttons & BTN_DPAD_DOWN)
        strcat(d, "D");
    if (cur->buttons & BTN_DPAD_LEFT)
        strcat(d, "L");
    if (cur->buttons & BTN_DPAD_RIGHT)
        strcat(d, "R");
    printf(
        "[hid] state: btn=%s dpad=%s lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d\n",
        btn,
        d[0] ? d : "-",
        cur->left_trigger,
        cur->right_trigger,
        cur->lx,
        cur->ly,
        cur->rx,
        cur->ry
    );
}
