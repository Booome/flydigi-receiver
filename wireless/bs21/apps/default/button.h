#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include "pinctrl.h"

typedef uint8_t button_t;

typedef void (*button_down_cb)(void *ctx);
typedef void (*button_up_cb)(uint32_t held_ms, void *ctx);
typedef void (*button_hold_cb)(uint32_t held_ms, void *ctx);

typedef struct {
    button_down_cb on_down;
    button_up_cb on_up;
    button_hold_cb on_hold;
} button_callbacks_t;

button_t button_init(pin_t port);
void button_set_cb(button_t btn, const button_callbacks_t *cb, void *ctx);

#endif /* BUTTON_H */
