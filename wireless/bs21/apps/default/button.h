#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include "pinctrl.h"

typedef uint8_t button_t;

typedef void (*button_down_cb)(void *ctx);
typedef void (*button_up_cb)(uint32_t held_ms, void *ctx);
typedef void (*button_hold_cb)(uint32_t held_ms, void *ctx);

button_t button_init(pin_t port);
void button_set_down_cb(button_t btn, button_down_cb cb, void *ctx);
void button_set_up_cb(button_t btn, button_up_cb cb, void *ctx);
void button_set_hold_cb(button_t btn, button_hold_cb cb, void *ctx);

#endif /* BUTTON_H */
