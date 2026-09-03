#ifndef SCAN_TABLE_H
#define SCAN_TABLE_H

#include "sle_common.h"
#include <stdbool.h>
#include <stdint.h>

#define SCAN_TABLE_SIZE 32

typedef struct {
    sle_addr_t addr;
    int8_t rssi;
    uint32_t count;
    uint8_t used;
} scan_device_t;

scan_device_t *scan_table_find(const sle_addr_t *addr);
scan_device_t *scan_table_add(const sle_addr_t *addr);
void scan_table_print(void);
void scan_table_reset(void);

#endif /* SCAN_TABLE_H */