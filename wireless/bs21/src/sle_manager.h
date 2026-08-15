#ifndef SLE_MANAGER_H
#define SLE_MANAGER_H

#include <stdint.h>

typedef struct {
    uint8_t addr[6];
    int8_t rssi;
    uint8_t data_len;
    uint8_t data[31];
} sle_scan_result_t;

typedef void (*scan_result_cb_t)(const sle_scan_result_t *result);
typedef void (*connect_state_cb_t)(uint8_t conn_id, uint8_t state, uint8_t reason);
typedef void (*pair_complete_cb_t)(uint8_t conn_id, uint8_t status);
typedef void (*ssap_data_cb_t)(uint8_t conn_id, const uint8_t *data, uint16_t len);

void sle_init(void);
void sle_start_announce(void);
void sle_stop_announce(void);
void sle_start_seek(void);
void sle_stop_seek(void);
void sle_connect(const uint8_t addr[6]);
void sle_disconnect(uint8_t conn_id);
void sle_pair(uint8_t conn_id);
void sle_send(uint8_t conn_id, const uint8_t *data, uint16_t len);
void sle_set_scan_callback(scan_result_cb_t cb);
void sle_set_connect_callback(connect_state_cb_t cb);
void sle_set_pair_callback(pair_complete_cb_t cb);
void sle_set_data_callback(ssap_data_cb_t cb);

#endif