#ifndef LED_H
#define LED_H

#include "pinctrl.h"
#include <stdbool.h>
#include <stdint.h>

typedef uint8_t led_t;

/* Fired on each blink toggle (software-timer flip), with the post-flip level.
 */
typedef void (*led_change_cb)(bool level, void *ctx);

led_t led_init(pin_t port);
void led_on(led_t led);
void led_off(led_t led);
void led_toggle(led_t led);
void led_blink(led_t led, uint32_t period_ms);
void led_stop_blinking(led_t led);
bool led_is_blinking(led_t led);
uint32_t led_get_blink_period(led_t led);
void led_set_change_cb(led_t led, led_change_cb cb, void *ctx);

#endif /* LED_H */
