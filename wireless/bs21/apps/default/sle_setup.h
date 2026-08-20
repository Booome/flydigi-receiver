#ifndef SLE_SETUP_H
#define SLE_SETUP_H

#include <stdint.h>

void sle_setup_rst(void);
void sle_setup_set_local_addr(void);
void sle_setup_handle_enable(uint8_t status);
void sle_scan_start(void);

#endif /* SLE_SETUP_H */
