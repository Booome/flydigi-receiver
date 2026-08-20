#include "led.h"
#include "gpio.h"
#include "pinctrl.h"
#include "systick.h"

#define LED_RED_PIN  S_MGPIO11
#define LED_BLUE_PIN S_MGPIO13

#define BTN_FB_START_MS  3000
#define BTN_FB_SOLID_MS  10000
#define BTN_FB_BLINK_MS  125

static bool g_blink_on = false;
static uint32_t g_last = 0;
static bool g_override = false;

static void led_set(pin_t pin, bool on)
{
    uapi_gpio_set_val(pin, on ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

static uint32_t led_now_ms(void)
{
    return (uint32_t)uapi_systick_get_ms();
}

void led_init(void)
{
    g_blink_on = false;
    g_last = 0;
    g_override = false;
    uapi_pin_set_mode(LED_RED_PIN, (pin_mode_t)HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(LED_RED_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_pin_set_mode(LED_BLUE_PIN, (pin_mode_t)HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(LED_BLUE_PIN, GPIO_DIRECTION_OUTPUT);
    led_set(LED_RED_PIN, false);
    led_set(LED_BLUE_PIN, false);
}

void led_red(bool on) { led_set(LED_RED_PIN, on); }
void led_blue(bool on) { led_set(LED_BLUE_PIN, on); }

void led_blink(uint32_t period_ms)
{
    uint32_t now_ms = led_now_ms();
    if (now_ms - g_last >= period_ms) {
        g_last = now_ms;
        g_blink_on = !g_blink_on;
        led_set(LED_BLUE_PIN, g_blink_on);
    }
}

void led_btn_feedback(uint32_t held_ms)
{
    if (held_ms == 0) {
        g_override = false;
        return;
    }
    if (held_ms < BTN_FB_START_MS) {
        return;
    }
    g_override = true;
    if (held_ms >= BTN_FB_SOLID_MS) {
        led_set(LED_BLUE_PIN, true);
    } else {
        led_blink(BTN_FB_BLINK_MS);
    }
}

bool led_is_override(void)
{
    return g_override;
}
