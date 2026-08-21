#ifndef RSSI_PICK_H
#define RSSI_PICK_H

#include <stdbool.h>
#include <stdint.h>

void rssi_pick_init(void);
bool rssi_pick_feed(const uint8_t addr[6], int8_t rssi);
const uint8_t *rssi_pick_locked_addr(void);

#endif /* RSSI_PICK_H */
