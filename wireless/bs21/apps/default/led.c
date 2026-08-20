#include "led.h"
#include "gpio.h"
#include "pinctrl.h"

#define LED_RED_PIN  S_MGPIO11
#define LED_BLUE_PIN S_MGPIO13

#define PAIR_BLINK_MS 250

static void led_set(pin_t pin, bool on)
{
    uapi_gpio_set_val(pin, on ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

void led_init(void)
{
    uapi_pin_set_mode(LED_RED_PIN, (pin_mode_t)HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(LED_RED_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_pin_set_mode(LED_BLUE_PIN, (pin_mode_t)HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(LED_BLUE_PIN, GPIO_DIRECTION_OUTPUT);
    led_set(LED_RED_PIN, false);
    led_set(LED_BLUE_PIN, false);
}

void led_red(bool on) { led_set(LED_RED_PIN, on); }
void led_blue(bool on) { led_set(LED_BLUE_PIN, on); }

static bool g_blink_on = false;
static uint32_t g_last = 0;

void led_pair_blink(uint32_t now_ms)
{
    if (now_ms - g_last >= PAIR_BLINK_MS) {
        g_last = now_ms;
        g_blink_on = !g_blink_on;
        led_set(LED_BLUE_PIN, g_blink_on);
    }
}