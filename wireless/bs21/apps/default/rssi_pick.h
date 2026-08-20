#ifndef RSSI_PICK_H
#define RSSI_PICK_H

#include <stdbool.h>
#include <stdint.h>

void rssi_pick_init(void);
void rssi_pick_tick(void);   /* advance the 10ms tick counter (call every 10ms) */
bool rssi_pick_feed(const uint8_t addr[6], int8_t rssi);
const uint8_t *rssi_pick_locked_addr(void);

#endif /* RSSI_PICK_H */
