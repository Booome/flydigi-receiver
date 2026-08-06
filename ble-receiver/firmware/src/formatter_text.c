#include "formatter_text.h"
#include <stdio.h>

/*
 * Format: LX:%06d,LY:%06d,RX:%06d,RY:%06d,BTN:%04x,LT:%03d,RT:%03d\r\n
 * Max output: 60 bytes (excluding null)
 */
size_t formatter_text_format(const struct controller_state *state,
                             char *buf, size_t buf_size)
{
    if (buf_size < FORMATTER_TEXT_MAX_LEN) {
        return 0;
    }

    int len = snprintf(buf, buf_size,
        "LX:%06d,LY:%06d,RX:%06d,RY:%06d,BTN:%04x,LT:%03d,RT:%03d\r\n",
        state->lx, state->ly, state->rx, state->ry,
        state->buttons, state->lt, state->rt);

    if (len < 0) {
        return 0;
    }

    return (size_t)len;
}
