#ifndef CONN_NV_H
#define CONN_NV_H

#include <stdbool.h>
#include <stdint.h>

void conn_nv_init(void);
bool conn_nv_load(uint8_t addr_out[6]);
bool conn_nv_save(const uint8_t addr[6]);
bool conn_nv_erase(void);
bool conn_nv_is_fatal(void);

#endif /* CONN_NV_H */
