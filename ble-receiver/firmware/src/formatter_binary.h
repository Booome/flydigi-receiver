#ifndef FORMATTER_BINARY_H
#define FORMATTER_BINARY_H

#include "controller_state.h"
#include <stddef.h>
#include <stdint.h>

#define FORMATTER_BINARY_FRAME_LEN 16

size_t formatter_binary_format(const struct controller_state *state,
                               uint8_t *buf, size_t buf_size);

#endif /* FORMATTER_BINARY_H */
