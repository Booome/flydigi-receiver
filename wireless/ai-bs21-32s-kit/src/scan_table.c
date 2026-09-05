#include "scan_table.h"
#include "securec.h"
#include "soc_osal.h"

static scan_device_t g_scan_table[SCAN_TABLE_SIZE] = {0};
static bool g_table_full = false;

scan_device_t *scan_table_find(const sle_addr_t *addr) {
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (g_scan_table[i].used &&
            memcmp(g_scan_table[i].addr.addr, addr->addr, SLE_ADDR_LEN) == 0) {
            return &g_scan_table[i];
        }
    }
    return NULL;
}

scan_device_t *scan_table_add(const sle_addr_t *addr) {
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (!g_scan_table[i].used) {
            g_scan_table[i].used = 1;
            memcpy_s(g_scan_table[i].addr.addr, SLE_ADDR_LEN, addr->addr, SLE_ADDR_LEN);
            return &g_scan_table[i];
        }
    }
    g_table_full = true;
    return NULL;
}

void scan_table_print(void) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (g_scan_table[i].used) {
            n++;
        }
    }
    osal_printk("[scan] devices:%u\r\n", n);
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (g_scan_table[i].used) {
            osal_printk(
                "  %u) %02x:%02x:%02x:%02x:%02x:%02x rssi:%d cnt:%u\r\n",
                i,
                g_scan_table[i].addr.addr[0],
                g_scan_table[i].addr.addr[1],
                g_scan_table[i].addr.addr[2],
                g_scan_table[i].addr.addr[3],
                g_scan_table[i].addr.addr[4],
                g_scan_table[i].addr.addr[5],
                g_scan_table[i].rssi,
                g_scan_table[i].count
            );
        }
    }
    if (g_table_full) {
        osal_printk("[scan] table full\r\n");
    }
}

scan_device_t *scan_table_best(void) {
    scan_device_t *best = NULL;
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        if (g_scan_table[i].used && (best == NULL || g_scan_table[i].rssi > best->rssi)) {
            best = &g_scan_table[i];
        }
    }
    return best;
}

void scan_table_reset(void) {
    for (uint8_t i = 0; i < SCAN_TABLE_SIZE; i++) {
        g_scan_table[i].used = 0;
        g_scan_table[i].count = 0;
        g_scan_table[i].rssi = 0;
    }
    g_table_full = false;
}
