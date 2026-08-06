#include <zephyr/kernel.h>
#include "controller_state.h"
#include "formatter_text.h"
#include "formatter_binary.h"
#include "output.h"

#define SIM_INTERVAL_MS 500

static void sim_update(struct controller_state *s)
{
    static int16_t stick = 0;
    static uint8_t trig = 0;
    static uint16_t btn = 0;
    static int tick = 0;

    /* Sticks: step 4096, wraps through full int16_t range in 16 steps */
    stick += 4096;
    s->lx = stick;
    s->ly = -stick;
    s->rx = stick / 2;
    s->ry = -stick / 2;

    /* Triggers: step 32, wraps 0-255 in 8 steps */
    trig += 32;
    s->lt = trig;
    s->rt = 255 - trig;

    /* Buttons: rotate one bit every 4 ticks (bit 0 -> bit 14) */
    if (++tick % 4 == 0) {
        if (btn == 0) {
            btn = 1;
        } else {
            btn = (btn << 1) | (btn >> 15);
        }
    }
    s->buttons = btn;
}

int main(void)
{
    printk("BLE Receiver M2 - simulated data\n");

    output_init();

    struct controller_state state = {0};

    while (1) {
        sim_update(&state);

#if IS_ENABLED(CONFIG_OUTPUT_FORMAT_TEXT)
        char buf[FORMATTER_TEXT_MAX_LEN];
        size_t len = formatter_text_format(&state, buf, sizeof(buf));
        output_send((const uint8_t *)buf, len);
#elif IS_ENABLED(CONFIG_OUTPUT_FORMAT_BINARY)
        uint8_t buf[FORMATTER_BINARY_FRAME_LEN];
        size_t len = formatter_binary_format(&state, buf, sizeof(buf));
        output_send(buf, len);
#endif

        k_sleep(K_MSEC(SIM_INTERVAL_MS));
    }

    return 0;
}
