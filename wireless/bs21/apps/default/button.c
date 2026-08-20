#include "button.h"
#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"

#define KEY_PIN          S_MGPIO0
#define LONG_PRESS_MS    3000
#define POLL_MS          10
#define LONG_PRESS_TICKS (LONG_PRESS_MS / POLL_MS) /* 300 ticks = 3s */

static void (*g_on_long)(void) = NULL;
static void (*g_on_short)(void) = NULL;

void button_set_cb(void (*on_long)(void), void (*on_short)(void))
{
    g_on_long = on_long;
    g_on_short = on_short;
}

void button_init(void)
{
    uapi_pin_set_mode(KEY_PIN, (pin_mode_t)HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(KEY_PIN, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(KEY_PIN, PIN_PULL_UP);
}

void *button_task(const char *arg)
{
    (void)arg;
    uint32_t held = 0;
    while (1) {
        gpio_level_t level = uapi_gpio_get_val(KEY_PIN);
        if (level == GPIO_LEVEL_LOW) {
            held++; /* pressed */
            if (held == LONG_PRESS_TICKS && g_on_long) {
                g_on_long(); /* fire once on 3s held */
            }
        } else {
            if (held != 0 && held < LONG_PRESS_TICKS && g_on_short) {
                g_on_short(); /* released before 3s => short press */
            }
            held = 0;
        }
        osal_msleep(POLL_MS);
    }
    return NULL;
}