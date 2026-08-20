#ifndef LED_H
#define LED_H

#include <stdbool.h>
#include <stdint.h>

void led_init(void);
void led_red(bool on);
void led_blue(bool on);
void led_pair_blink(uint32_t now_ms);

#endif /* LED_H */