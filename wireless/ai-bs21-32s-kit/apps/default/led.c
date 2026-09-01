#include "led.h"
#include "cmsis_os2.h"
#include "gpio.h"

#define LED_MAX 8

#define MS2TICK(ms)                                                            \
  ((uint32_t)(((uint64_t)(ms) * osKernelGetTickFreq()) / 1000))

typedef struct {
  bool used;
  pin_t pin;
  osTimerId_t timer;
  uint32_t period_ms;
  bool level;
  led_change_cb change_cb;
  void *change_ctx;
} led_inst_t;

static led_inst_t g_leds[LED_MAX];

static void led_timer_cb(void *arg) {
  led_t idx = (led_t)(uintptr_t)arg;
  led_inst_t *l = &g_leds[idx];
  l->level = !l->level;
  uapi_gpio_set_val(l->pin, l->level ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
  if (l->change_cb != NULL) {
    l->change_cb(l->level, l->change_ctx);
  }
}

static led_inst_t *led_get(led_t led) {
  if (led >= LED_MAX || !g_leds[led].used) {
    return NULL;
  }
  return &g_leds[led];
}

led_t led_init(pin_t port) {
  for (led_t i = 0; i < LED_MAX; i++) {
    if (g_leds[i].used) {
      continue;
    }
    uapi_pin_set_mode(port, (pin_mode_t)HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(port, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(port, GPIO_LEVEL_LOW);
    g_leds[i].used = true;
    g_leds[i].pin = port;
    g_leds[i].period_ms = 0;
    g_leds[i].level = false;
    g_leds[i].change_cb = NULL;
    g_leds[i].change_ctx = NULL;
    g_leds[i].timer =
        osTimerNew(led_timer_cb, osTimerPeriodic, (void *)(uintptr_t)i, NULL);
    return i;
  }
  return (led_t)-1;
}

void led_on(led_t led) {
  led_inst_t *l = led_get(led);
  if (l == NULL) {
    return;
  }
  if (l->timer != NULL) {
    osTimerStop(l->timer);
  }
  l->period_ms = 0;
  l->level = true;
  uapi_gpio_set_val(l->pin, GPIO_LEVEL_HIGH);
}

void led_off(led_t led) {
  led_inst_t *l = led_get(led);
  if (l == NULL) {
    return;
  }
  if (l->timer != NULL) {
    osTimerStop(l->timer);
  }
  l->period_ms = 0;
  l->level = false;
  uapi_gpio_set_val(l->pin, GPIO_LEVEL_LOW);
}

void led_toggle(led_t led) {
  led_inst_t *l = led_get(led);
  if (l == NULL) {
    return;
  }
  l->level = !l->level;
  uapi_gpio_set_val(l->pin, l->level ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

void led_blink(led_t led, uint32_t period_ms) {
  led_inst_t *l = led_get(led);
  if (l == NULL || period_ms == 0) {
    return;
  }
  if (l->period_ms == period_ms) {
    return;
  }
  if (l->timer != NULL) {
    osTimerStop(l->timer);
  }
  l->period_ms = period_ms;
  l->level = false;
  uapi_gpio_set_val(l->pin, GPIO_LEVEL_LOW);
  osTimerStart(l->timer, MS2TICK(period_ms));
}

void led_stop_blinking(led_t led) {
  led_inst_t *l = led_get(led);
  if (l == NULL) {
    return;
  }
  if (l->timer != NULL) {
    osTimerStop(l->timer);
  }
  l->period_ms = 0;
}

bool led_is_blinking(led_t led) {
  led_inst_t *l = led_get(led);
  if (l == NULL) {
    return false;
  }
  return l->period_ms != 0;
}

uint32_t led_get_blink_period(led_t led) {
  led_inst_t *l = led_get(led);
  if (l == NULL) {
    return 0;
  }
  return l->period_ms;
}

void led_set_change_cb(led_t led, led_change_cb cb, void *ctx) {
  led_inst_t *l = led_get(led);
  if (l == NULL) {
    return;
  }
  l->change_cb = cb;
  l->change_ctx = ctx;
}
