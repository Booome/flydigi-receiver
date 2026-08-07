#include <zephyr/ztest.h>
#include <stdio.h>
#include <string.h>
#include "controller_state.h"
#include "formatter_text.h"

ZTEST_SUITE(formatter_text_tests, NULL, NULL, NULL, NULL, NULL);

/* Expected output for zero state (64 bytes):
 * LX:000000,LY:000000,RX:000000,RY:000000,BTN:0000,LT:000,RT:000\r\n
 */
ZTEST(formatter_text_tests, test_zero_state)
{
    struct controller_state state = {0};
    char buf[FORMATTER_TEXT_MAX_LEN];
    size_t len = formatter_text_format(&state, buf, sizeof(buf));

    zassert_equal(len, 64, "expected 64, got %zu", len);
    zassert_str_equal(buf, "LX:000000,LY:000000,RX:000000,RY:000000,BTN:0000,LT:000,RT:000\r\n");
}

ZTEST(formatter_text_tests, test_stick_max)
{
    struct controller_state state = {
        .lx = 32767, .ly = 32767, .rx = 32767, .ry = 32767,
    };
    char buf[FORMATTER_TEXT_MAX_LEN];
    size_t len = formatter_text_format(&state, buf, sizeof(buf));

    zassert_equal(len, 64, "expected 64, got %zu", len);
    zassert_str_equal(buf, "LX:032767,LY:032767,RX:032767,RY:032767,BTN:0000,LT:000,RT:000\r\n");
}

ZTEST(formatter_text_tests, test_stick_min)
{
    struct controller_state state = {
        .lx = -32768, .ly = -32768, .rx = -32768, .ry = -32768,
    };
    char buf[FORMATTER_TEXT_MAX_LEN];
    size_t len = formatter_text_format(&state, buf, sizeof(buf));

    zassert_equal(len, 64, "expected 64, got %zu", len);
    zassert_str_equal(buf, "LX:-32768,LY:-32768,RX:-32768,RY:-32768,BTN:0000,LT:000,RT:000\r\n");
}

ZTEST(formatter_text_tests, test_trigger_max)
{
    struct controller_state state = { .lt = 255, .rt = 255 };
    char buf[FORMATTER_TEXT_MAX_LEN];
    formatter_text_format(&state, buf, sizeof(buf));

    zassert_str_equal(buf, "LX:000000,LY:000000,RX:000000,RY:000000,BTN:0000,LT:255,RT:255\r\n");
}

ZTEST(formatter_text_tests, test_trigger_mid)
{
    struct controller_state state = { .lt = 128, .rt = 128 };
    char buf[FORMATTER_TEXT_MAX_LEN];
    formatter_text_format(&state, buf, sizeof(buf));

    zassert_str_equal(buf, "LX:000000,LY:000000,RX:000000,RY:000000,BTN:0000,LT:128,RT:128\r\n");
}

ZTEST(formatter_text_tests, test_buttons_all)
{
    struct controller_state state = { .buttons = 0xFFFF };
    char buf[FORMATTER_TEXT_MAX_LEN];
    formatter_text_format(&state, buf, sizeof(buf));

    /* Check BTN field is ffff */
    zassert_mem_equal(buf + 40, "BTN:ffff", 8);
}

ZTEST(formatter_text_tests, test_buttons_single)
{
    char buf[FORMATTER_TEXT_MAX_LEN];
    struct controller_state state = {0};

    for (int i = 0; i < 15; i++) {
        state.buttons = BIT(i);
        formatter_text_format(&state, buf, sizeof(buf));
        char expected[9];
        snprintf(expected, sizeof(expected), "BTN:%04x", BIT(i));
        zassert_mem_equal(buf + 40, expected, 8,
                          "button bit %d failed", i);
    }
}

ZTEST(formatter_text_tests, test_format_stable)
{
    struct controller_state state = {
        .lx = 12345, .ly = -5432, .rx = 32767, .ry = -32768,
        .buttons = 0x0F0F, .lt = 200, .rt = 50,
    };
    char buf1[FORMATTER_TEXT_MAX_LEN];
    char buf2[FORMATTER_TEXT_MAX_LEN];
    size_t len1, len2;

    for (int i = 0; i < 10; i++) {
        len1 = formatter_text_format(&state, buf1, sizeof(buf1));
        len2 = formatter_text_format(&state, buf2, sizeof(buf2));
        zassert_equal(len1, len2, "length changed on iteration %d", i);
        zassert_mem_equal(buf1, buf2, len1, "output changed on iteration %d", i);
    }
}

ZTEST(formatter_text_tests, test_crlf)
{
    struct controller_state state = {0};
    char buf[FORMATTER_TEXT_MAX_LEN];
    size_t len = formatter_text_format(&state, buf, sizeof(buf));

    zassert_equal(buf[len - 2], '\r', "expected \\r before end");
    zassert_equal(buf[len - 1], '\n', "expected \\n at end");
}

ZTEST(formatter_text_tests, test_buffer_overflow)
{
    struct controller_state state = {0};
    char buf[10];
    size_t len = formatter_text_format(&state, buf, sizeof(buf));

    zassert_equal(len, 0, "expected 0 for small buffer, got %zu", len);
}
