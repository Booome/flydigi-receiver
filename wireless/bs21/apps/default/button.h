#ifndef BUTTON_H
#define BUTTON_H

typedef void (*button_cb_t)(void);

void button_init(void);
void button_set_cb(button_cb_t on_long, button_cb_t on_short, button_cb_t on_very_long);
void *button_task(const char *arg);

#endif /* BUTTON_H */
