#include "button.h"
#include "gpio.h"
#include "cmsis_os2.h"

#define BTN_MAX 4
#define POLL_MS 10

static const uint32_t HOLD_MARKS_MS[] = { 3000, 10000 };
#define HOLD_MARKS_N (sizeof(HOLD_MARKS_MS) / sizeof(HOLD_MARKS_MS[0]))

#define MS2TICK(ms) \
    ((uint32_t)(((uint64_t)(ms) * osKernelGetTickFreq()) / 1000))

typedef struct {
    bool used;
    pin_t pin;
    osTimerId_t timer;
    uint32_t held_ms;
    bool pressed;
    uint32_t last_hold_idx;
    button_callbacks_t cb;
    void *ctx;
} btn_inst_t;

static btn_inst_t g_btns[BTN_MAX];

static void btn_poll_cb(void *arg)
{
    button_t idx = (button_t)(uintptr_t)arg;
    btn_inst_t *b = &g_btns[idx];
    gpio_level_t level = uapi_gpio_get_val(b->pin);

    if (level == GPIO_LEVEL_LOW) {
        if (!b->pressed) {
            b->pressed = true;
            b->held_ms = 0;
            b->last_hold_idx = 0;
            if (b->cb.on_down != NULL) {
                b->cb.on_down(b->ctx);
            }
        }
        b->held_ms += POLL_MS;
        while (b->last_hold_idx < HOLD_MARKS_N &&
               b->held_ms >= HOLD_MARKS_MS[b->last_hold_idx]) {
            uint32_t mark = HOLD_MARKS_MS[b->last_hold_idx];
            b->last_hold_idx++;
            if (b->cb.on_hold != NULL) {
                b->cb.on_hold(mark, b->ctx);
            }
        }
    } else if (b->pressed) {
        if (b->cb.on_up != NULL) {
            b->cb.on_up(b->held_ms, b->ctx);
        }
        b->pressed = false;
    }
}

button_t button_init(pin_t port)
{
    for (button_t i = 0; i < BTN_MAX; i++) {
        if (g_btns[i].used) {
            continue;
        }
        uapi_pin_set_mode(port, (pin_mode_t)HAL_PIO_FUNC_GPIO);
        uapi_gpio_set_dir(port, GPIO_DIRECTION_INPUT);
        uapi_pin_set_pull(port, PIN_PULL_UP);
        g_btns[i].used = true;
        g_btns[i].pin = port;
        g_btns[i].held_ms = 0;
        g_btns[i].pressed = false;
        g_btns[i].last_hold_idx = 0;
        g_btns[i].timer = osTimerNew(btn_poll_cb, osTimerPeriodic,
                                     (void *)(uintptr_t)i, NULL);
        osTimerStart(g_btns[i].timer, MS2TICK(POLL_MS));
        return i;
    }
    return (button_t)-1;
}

void button_set_cb(button_t btn, const button_callbacks_t *cb, void *ctx)
{
    if (btn >= BTN_MAX || !g_btns[btn].used || cb == NULL) {
        return;
    }
    g_btns[btn].cb = *cb;
    g_btns[btn].ctx = ctx;
}
