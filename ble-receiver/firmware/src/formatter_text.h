#ifndef FORMATTER_TEXT_H
#define FORMATTER_TEXT_H

#include "controller_state.h"
#include <stddef.h>

#define FORMATTER_TEXT_MAX_LEN 65  /* actual output is 64 bytes + null */

/*
 * Format controller_state into a text line.
 * Format: LX:%06d,LY:%06d,RX:%06d,RY:%06d,BTN:%04x,LT:%03d,RT:%03d\r\n
 * Returns bytes written (excluding null terminator).
 * Returns 0 if buf_size is too small.
 * Actual output length is always 64 bytes.
 */
size_t formatter_text_format(const struct controller_state *state,
                             char *buf, size_t buf_size);

#endif /* FORMATTER_TEXT_H */
