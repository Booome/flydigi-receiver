/*
 * SPDX-FileCopyrightText: 2026 flydigi-receiver
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * default: BT HID host connect + raw HID report capture.
 *
 * 三层候选算法（spec §四）：
 *   层1 语义过滤：只保留 gamepad-class BR/EDR（COD major=5 minor=2）
 *   层2 EWMA 平滑：0.3*新 + 0.7*旧，去除 ±3~5dB RSSI 抖动
 *   层3 迟滞 + 兜底：3dB 迟滞，3s 稳定或 8s 强制连
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_hid_gap.h"
#include "esp_hidh.h"
#include "esp_hidh_gattc.h"

#define TAG "default"
#define SCAN_DURATION_SEC 5
#define HYSTERESIS_DB 3
#define LOCK_WAIT_MS 3000
#define MAX_WAIT_MS 8000
#define EWMA_ALPHA_NUM 3 /* /10 → alpha=0.3 */
#define EWMA_ALPHA_DEN 10
#define CANDIDATE_GONE_ROUNDS 2
#define EWMA_MAX 16

static const char *transport_str(esp_hid_transport_t t) {
    switch (t) {
    case ESP_HID_TRANSPORT_BLE:
        return "BLE";
    case ESP_HID_TRANSPORT_BT:
        return "BR_EDR";
    default:
        return "?";
    }
}

static void print_bda(const uint8_t *bda) {
    printf("%02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

static uint8_t bda_eq(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 6) == 0;
}

/* Per-device EWMA RSSI: key = (bda, transport) */
typedef struct {
    uint8_t bda[6];
    esp_hid_transport_t transport;
    bool used;
    float smoothed;
} ewma_entry_t;
static ewma_entry_t g_ewma[EWMA_MAX];

static float *ewma_get_or_create(const uint8_t *bda, esp_hid_transport_t t) {
    for (int i = 0; i < EWMA_MAX; i++) {
        if (g_ewma[i].used && bda_eq(g_ewma[i].bda, bda) && g_ewma[i].transport == t) {
            return &g_ewma[i].smoothed;
        }
    }
    for (int i = 0; i < EWMA_MAX; i++) {
        if (!g_ewma[i].used) {
            memcpy(g_ewma[i].bda, bda, 6);
            g_ewma[i].transport = t;
            g_ewma[i].used = true;
            g_ewma[i].smoothed = -127.0f;
            return &g_ewma[i].smoothed;
        }
    }
    return NULL;
}

static void ewma_clear(void) {
    for (int i = 0; i < EWMA_MAX; i++)
        g_ewma[i].used = false;
}

/* Candidate state */
typedef struct {
    bool active;
    uint8_t bda[6];
    esp_hid_transport_t transport;
    float smoothed;
    int64_t set_at_ms;
} candidate_t;
static candidate_t g_candidate = {0};
static int g_not_seen_count = 0;
static int64_t g_scan_start_ms = 0;
static volatile bool g_connected = false;

static bool is_gamepad(const esp_hid_scan_result_t *r) {
    /* 层1: 仅 gamepad-class BR/EDR. 实测 Apex5: major=PERIPHERAL(5) minor=2. */
    if (r->transport != ESP_HID_TRANSPORT_BT)
        return false;
    if (r->bt.cod.major != 5)
        return false;
    if (r->bt.cod.minor != 2)
        return false;
    return true;
}

static void reset_to_scan(void) {
    memset(&g_candidate, 0, sizeof(g_candidate));
    ewma_clear();
    g_not_seen_count = 0;
    g_scan_start_ms = esp_timer_get_time() / 1000;
}

static void hidh_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                               void *event_data) {
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;
    const uint8_t *bda = NULL;
    esp_hid_transport_t transport = ESP_HID_TRANSPORT_BT;

    switch (event_id) {
    case ESP_HIDH_OPEN_EVENT:
        bda = esp_hidh_dev_bda_get(param->open.dev);
        transport = esp_hidh_dev_transport_get(param->open.dev);
        if (param->open.status == ESP_OK) {
            g_connected = true;
            printf("[hid] open: addr=");
            print_bda(bda);
            printf(" transport=%s\n", transport_str(transport));
        } else {
            printf("[hid] open FAIL: addr=");
            print_bda(bda);
            printf(" transport=%s status=0x%x\n", transport_str(transport),
                   (unsigned)param->open.status);
            reset_to_scan();
        }
        break;
    case ESP_HIDH_INPUT_EVENT:
        bda = esp_hidh_dev_bda_get(param->input.dev);
        transport = esp_hidh_dev_transport_get(param->input.dev);
        printf("[hid] report: addr=");
        print_bda(bda);
        printf(" transport=%s len=%d data=", transport_str(transport), param->input.length);
        for (uint16_t i = 0; i < param->input.length; i++) {
            printf("%02x", param->input.data[i]);
        }
        printf("\n");
        break;
    case ESP_HIDH_CLOSE_EVENT:
        bda = esp_hidh_dev_bda_get(param->close.dev);
        transport = esp_hidh_dev_transport_get(param->close.dev);
        printf("[hid] close: addr=");
        print_bda(bda);
        printf(" transport=%s status=0x%x\n", transport_str(transport),
               (unsigned)param->close.status);
        if (param->close.dev) {
            esp_hidh_dev_free(param->close.dev);
        }
        g_connected = false;
        reset_to_scan();
        break;
    default:
        break;
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_hid_gap_init(ESP_BT_MODE_BTDM));
    ESP_LOGI(TAG, "BTDM initialized");

#if CONFIG_BT_BLE_ENABLED
    /* esp_hid_scan() blocks on a BLE semaphore; need GATTC callback registered */
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler));
#endif

    esp_hidh_config_t hidh_cfg = {
        .callback = hidh_event_handler,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(esp_hidh_init(&hidh_cfg));

    g_scan_start_ms = esp_timer_get_time() / 1000;

    while (1) {
        esp_hid_scan_result_t *results = NULL;
        size_t n = 0;
        esp_err_t sr = esp_hid_scan(SCAN_DURATION_SEC, &n, &results);
        if (sr != ESP_OK) {
            ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(sr));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* 层1: 只保留 gamepad-class BR/EDR */
        esp_hid_scan_result_t *best = NULL;
        for (size_t i = 0; i < n; i++) {
            if (is_gamepad(&results[i])) {
                if (!best || results[i].rssi > best->rssi) {
                    best = &results[i];
                }
            }
        }

        if (!best) {
            if (g_candidate.active && g_not_seen_count >= CANDIDATE_GONE_ROUNDS) {
                printf("[hid] candidate gone\n");
                memset(&g_candidate, 0, sizeof(g_candidate));
            }
            if (g_candidate.active)
                g_not_seen_count++;
            esp_hid_scan_results_free(results);
            continue;
        }

        /* 层2: EWMA 平滑 */
        float *smoothed_p = ewma_get_or_create(best->bda, best->transport);
        if (!smoothed_p) {
            esp_hid_scan_results_free(results);
            continue;
        }
        if (*smoothed_p == -127.0f) {
            *smoothed_p = (float)best->rssi; /* 首次采到 */
        } else {
            *smoothed_p = (float)best->rssi * EWMA_ALPHA_NUM / EWMA_ALPHA_DEN +
                          *smoothed_p * (EWMA_ALPHA_DEN - EWMA_ALPHA_NUM) / EWMA_ALPHA_DEN;
        }

        /* 层3: 迟滞 + 锁定计时 */
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (!g_candidate.active) {
            g_candidate.active = true;
            memcpy(g_candidate.bda, best->bda, 6);
            g_candidate.transport = best->transport;
            g_candidate.smoothed = *smoothed_p;
            g_candidate.set_at_ms = now_ms;
            g_not_seen_count = 0;
            printf("[hid] candidate: addr=");
            print_bda(best->bda);
            printf(" smoothed=%.1f\n", g_candidate.smoothed);
        } else if (bda_eq(best->bda, g_candidate.bda) && best->transport == g_candidate.transport) {
            /* 同候选: 计时继续朝 LOCK_WAIT_MS 走 */
            g_not_seen_count = 0;
        } else if (*smoothed_p >= g_candidate.smoothed + HYSTERESIS_DB) {
            /* 新候选平滑 RSSI 比候选大 >= 3dB → 明显更强; 替换 + 重置计时 */
            g_candidate.active = true;
            memcpy(g_candidate.bda, best->bda, 6);
            g_candidate.transport = best->transport;
            g_candidate.smoothed = *smoothed_p;
            g_candidate.set_at_ms = now_ms;
            g_not_seen_count = 0;
            printf("[hid] candidate replace: addr=");
            print_bda(best->bda);
            printf(" smoothed=%.1f\n", g_candidate.smoothed);
        }
        /* else: 接近打平, 保留, 不重置计时 */

        /* 锁定条件: 稳定 3s 或超 8s 兜底 */
        int64_t now = esp_timer_get_time() / 1000;
        int64_t stable_elapsed = now - g_candidate.set_at_ms;
        int64_t total_elapsed = now - g_scan_start_ms;
        if (stable_elapsed >= LOCK_WAIT_MS || total_elapsed >= MAX_WAIT_MS) {
            esp_hidh_dev_open(g_candidate.bda, g_candidate.transport, 0);
            /* open/close events clear state via reset_to_scan() in callback */
            reset_to_scan();
        }

        esp_hid_scan_results_free(results);
    }
}