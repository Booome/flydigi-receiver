#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include "controller_state.h"
#include "formatter_binary.h"

ZTEST_SUITE(formatter_binary_tests, NULL, NULL, NULL, NULL, NULL);

static uint8_t calc_checksum(const uint8_t *buf)
{
    uint8_t sum = 0;
    for (int i = 2; i <= 14; i++) {
        sum += buf[i];
    }
    return sum;
}

ZTEST(formatter_binary_tests, test_frame_header)
{
    struct controller_state state = {0};
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[0], 0xAA, "expected 0xAA, got 0x%02x", buf[0]);
    zassert_equal(buf[1], 0x55, "expected 0x55, got 0x%02x", buf[1]);
}

ZTEST(formatter_binary_tests, test_length_field)
{
    struct controller_state state = {0};
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[2], 13, "expected 13, got %d", buf[2]);
}

ZTEST(formatter_binary_tests, test_buttons_le)
{
    struct controller_state state = { .buttons = 0xABCD };
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[3], 0xCD, "low byte");
    zassert_equal(buf[4], 0xAB, "high byte");
    zassert_equal(sys_get_le16(&buf[3]), 0xABCD, "LE readback");
}

ZTEST(formatter_binary_tests, test_sticks_le)
{
    struct controller_state state = {
        .lx = 12345, .ly = -5432, .rx = 32767, .ry = -32768,
    };
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal((int16_t)sys_get_le16(&buf[5]), 12345, "lx");
    zassert_equal((int16_t)sys_get_le16(&buf[7]), -5432, "ly");
    zassert_equal((int16_t)sys_get_le16(&buf[9]), 32767, "rx");
    zassert_equal((int16_t)sys_get_le16(&buf[11]), -32768, "ry");
}

ZTEST(formatter_binary_tests, test_triggers)
{
    struct controller_state state = { .lt = 200, .rt = 55 };
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[13], 200, "lt");
    zassert_equal(buf[14], 55, "rt");
}

ZTEST(formatter_binary_tests, test_checksum_zero)
{
    struct controller_state state = {0};
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[15], calc_checksum(buf), "checksum");
}

ZTEST(formatter_binary_tests, test_checksum_max)
{
    struct controller_state state = {
        .buttons = 0xFFFF, .lx = 32767, .ly = 32767,
        .rx = 32767, .ry = 32767, .lt = 255, .rt = 255,
    };
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[15], calc_checksum(buf), "checksum");
}

ZTEST(formatter_binary_tests, test_checksum_random)
{
    struct controller_state state = {
        .buttons = 0x0F0F, .lx = -12345, .ly = 24680,
        .rx = -1, .ry = 1, .lt = 128, .rt = 64,
    };
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(buf[15], calc_checksum(buf), "checksum");
}

ZTEST(formatter_binary_tests, test_frame_length)
{
    struct controller_state state = {0};
    uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
    size_t len = formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(len, FORMATTER_BINARY_FRAME_LEN, "len");
}

ZTEST(formatter_binary_tests, test_buffer_overflow)
{
    struct controller_state state = {0};
    uint8_t buf[10];
    size_t len = formatter_binary_format(&state, buf, sizeof(buf));
    zassert_equal(len, 0, "expected 0");
}
