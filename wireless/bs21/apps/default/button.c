#include "button.h"
#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"

#define KEY_PIN          S_MGPIO0
#define LONG_PRESS_MS    3000
#define VERY_LONG_PRESS_MS 10000
#define POLL_MS          10
#define LONG_PRESS_TICKS (LONG_PRESS_MS / POLL_MS)      /* 300 ticks = 3s */
#define VERY_LONG_PRESS_TICKS (VERY_LONG_PRESS_MS / POLL_MS) /* 1000 ticks = 10s */

static button_cb_t g_on_long = NULL;
static button_cb_t g_on_short = NULL;
static button_cb_t g_on_very_long = NULL;

void button_set_cb(button_cb_t on_long, button_cb_t on_short,
                   button_cb_t on_very_long)
{
    g_on_long = on_long;
    g_on_short = on_short;
    g_on_very_long = on_very_long;
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
            held++;
            if (held == LONG_PRESS_TICKS) {
                osal_printk("[btn] long press threshold (3s)\r\n");
            } else if (held == VERY_LONG_PRESS_TICKS) {
                osal_printk("[btn] very long press threshold (10s)\r\n");
            }
        } else {
            if (held != 0) {
                if (held < LONG_PRESS_TICKS && g_on_short) {
                    g_on_short();
                } else if (held < VERY_LONG_PRESS_TICKS && g_on_long) {
                    g_on_long();
                } else if (g_on_very_long) {
                    g_on_very_long();
                }
            }
            held = 0;
        }
        osal_msleep(POLL_MS);
    }
    return NULL;
}
