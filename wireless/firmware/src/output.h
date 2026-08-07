#ifndef OUTPUT_H
#define OUTPUT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Initialize the output backend.
 * Returns 0 on success, negative errno on failure.
 */
int output_init(void);

/*
 * Send data through the output backend.
 * Returns 0 on success (data is discarded if backend not ready).
 * Returns negative errno on error.
 */
int output_send(const uint8_t *data, size_t len);

#endif /* OUTPUT_H */
