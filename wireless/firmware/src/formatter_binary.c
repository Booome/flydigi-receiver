#include "formatter_binary.h"
#include <zephyr/sys/byteorder.h>

#define FRAME_HEAD1 0xAA
#define FRAME_HEAD2 0x55
#define FRAME_PAYLOAD_LEN 13

size_t formatter_binary_format(const struct controller_state *state,
                               uint8_t *buf, size_t buf_size)
{
    if (buf_size < FORMATTER_BINARY_FRAME_LEN) {
        return 0;
    }

    buf[0] = FRAME_HEAD1;
    buf[1] = FRAME_HEAD2;
    buf[2] = FRAME_PAYLOAD_LEN;

    sys_put_le16(state->buttons, &buf[3]);
    sys_put_le16((uint16_t)state->lx, &buf[5]);
    sys_put_le16((uint16_t)state->ly, &buf[7]);
    sys_put_le16((uint16_t)state->rx, &buf[9]);
    sys_put_le16((uint16_t)state->ry, &buf[11]);

    buf[13] = state->lt;
    buf[14] = state->rt;

    uint8_t checksum = 0;
    for (int i = 2; i <= 14; i++) {
        checksum += buf[i];
    }
    buf[15] = checksum;

    return FORMATTER_BINARY_FRAME_LEN;
}
