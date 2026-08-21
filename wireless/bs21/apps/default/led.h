#ifndef LED_H
#define LED_H

#include <stdbool.h>
#include <stdint.h>
#include "pinctrl.h"

typedef uint8_t led_t;

led_t led_init(pin_t port);
void led_on(led_t led);
void led_off(led_t led);
void led_toggle(led_t led);
void led_blink(led_t led, uint32_t period_ms);
void led_stop_blinking(led_t led);
bool led_is_blinking(led_t led);
uint32_t led_get_blink_period(led_t led);

#endif /* LED_H */
