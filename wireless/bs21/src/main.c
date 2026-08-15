#include <stdio.h>
#include "sle_manager.h"

static void on_scan_result(const sle_scan_result_t *result)
{
    printf("[SCAN] device: ");
    for (int i = 0; i < 6; i++) {
        printf("%02X", result->addr[i]);
        if (i < 5) printf(":");
    }
    printf(" RSSI=%d data_len=%d\n", result->rssi, result->data_len);
    if (result->data_len > 0) {
        printf("[SCAN]   data: ");
        for (int i = 0; i < result->data_len; i++) {
            printf("%02X ", result->data[i]);
        }
        printf("\n");
    }
}

int main(void)
{
    printf("BS21 G-Node (Seek)\n");
    sle_init();
    sle_set_scan_callback(on_scan_result);
    sle_start_seek();
    while (1) {
        // SLE stack event loop
    }
    return 0;
}