#ifndef LED_H
#define LED_H

#include <stdbool.h>
#include <stdint.h>

void led_init(void);
void led_red(bool on);
void led_blue(bool on);
void led_blink(uint32_t period_ms);
void led_btn_feedback(uint32_t held_ms);
bool led_is_override(void);

#endif /* LED_H */
