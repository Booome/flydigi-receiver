#ifndef BUTTON_H
#define BUTTON_H

void button_init(void);
void button_set_cb(void (*on_long)(void), void (*on_short)(void));
void *button_task(const char *arg);

#endif /* BUTTON_H */
