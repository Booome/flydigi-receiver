# default app LED/button 重构实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 default app 的 LED/button 模块重构为"句柄 + 软定时器 + 命令/事件"统一模式，并让 conn_mgr 去掉全局轮询 tick，改事件 + 软定时器驱动。

**Architecture:** LED 模块句柄化（`led_init(pin)` 返回 `led_t`，blink 用 osTimer 软定时器）；button 模块句柄化 + 事件回调（down/up/hold）；rssi_pick 从 tick 计数改为毫秒时间戳；conn_mgr 去 `tick_task`，状态转换时显式驱动 LED、配对超时用软定时器、注册 button 回调。main.c 去掉三个 task。

**Tech Stack:** Ai-BS21_SDK（LiteOS swtmr + CMSIS RTOS2 osTimer）、BS21 板载固件（C）、CMake

## Global Constraints

- SDK 只读引用：不修改 `~/.local/Ai-BS21_SDK` 任何源码。
- `-Werror`，所有编译 0 warning。
- 只改 `wireless/bs21/apps/default/` 下：`led.c/h`、`button.c/h`、`conn_mgr.c/h`、`rssi_pick.c/h`、`main.c`。不改 `src/`、`sle_probe`、其他 app、SDK。
- 代码注释英文、简短（只写 why）。
- 提交信息风格 `<type>: <subject>`。
- 每个 task 结束后 run code-simplifier 技能（若可用）做简化 pass 并保持编译通过。
- 新增软定时器共约 4 个（LED red/blue blink、button 采样、conn_mgr 配对超时），低于 swtmr 默认上限 16。
- 软定时器单位是 tick，用 `osKernelGetTickFreq()` 换算 ms→tick。

---

### Task 1: LED 模块重写（句柄 + 软定时器）

**Files:**
- Modify: `wireless/bs21/apps/default/led.h`（整体替换）
- Modify: `wireless/bs21/apps/default/led.c`（整体替换）
- Modify: `wireless/bs21/apps/default/main.c`（注册 led 句柄）
- Modify: `wireless/bs21/apps/default/conn_mgr.c` / `conn_mgr.h`（接收 led 句柄、适配 LED 调用、移除 led_btn_feedback/led_is_override 依赖）
- Modify: `wireless/bs21/apps/default/button.c`（移除 led_btn_feedback 调用）

**Interfaces:**
- Produces（LED 模块新接口）:
  - `typedef uint8_t led_t;`
  - `led_t led_init(pin_t port);`
  - `void led_on(led_t led); void led_off(led_t led); void led_toggle(led_t led);`
  - `void led_blink(led_t led, uint32_t period_ms); void led_stop_blinking(led_t led);`
  - `bool led_is_blinking(led_t led); uint32_t led_get_blink_period(led_t led);`
- Produces（conn_mgr 新签名）: `void conn_mgr_init(led_t led_red, led_t led_blue);`

- [ ] **Step 1: 替换 led.h**

`wireless/bs21/apps/default/led.h`：

```c
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
```

- [ ] **Step 2: 替换 led.c**

`wireless/bs21/apps/default/led.c`：

```c
#include "led.h"
#include "gpio.h"
#include "cmsis_os2.h"

#define LED_MAX 8

#define MS2TICK(ms) \
    ((uint32_t)(((uint64_t)(ms) * osKernelGetTickFreq()) / 1000))

typedef struct {
    bool used;
    pin_t pin;
    osTimerId_t timer;
    uint32_t period_ms;
    bool level;
} led_inst_t;

static led_inst_t g_leds[LED_MAX];

static void led_timer_cb(void *arg)
{
    led_t idx = (led_t)(uintptr_t)arg;
    led_inst_t *l = &g_leds[idx];
    l->level = !l->level;
    uapi_gpio_set_val(l->pin, l->level ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

static led_inst_t *led_get(led_t led)
{
    if (led >= LED_MAX || !g_leds[led].used) {
        return NULL;
    }
    return &g_leds[led];
}

led_t led_init(pin_t port)
{
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
        g_leds[i].timer = osTimerNew(led_timer_cb, osTimerPeriodic,
                                     (void *)(uintptr_t)i, NULL);
        return i;
    }
    return (led_t)-1;
}

void led_on(led_t led)
{
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

void led_off(led_t led)
{
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

void led_toggle(led_t led)
{
    led_inst_t *l = led_get(led);
    if (l == NULL) {
        return;
    }
    l->level = !l->level;
    uapi_gpio_set_val(l->pin, l->level ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

void led_blink(led_t led, uint32_t period_ms)
{
    led_inst_t *l = led_get(led);
    if (l == NULL || period_ms == 0) {
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

void led_stop_blinking(led_t led)
{
    led_inst_t *l = led_get(led);
    if (l == NULL) {
        return;
    }
    if (l->timer != NULL) {
        osTimerStop(l->timer);
    }
    l->period_ms = 0;
}

bool led_is_blinking(led_t led)
{
    led_inst_t *l = led_get(led);
    if (l == NULL) {
        return false;
    }
    return l->period_ms != 0;
}

uint32_t led_get_blink_period(led_t led)
{
    led_inst_t *l = led_get(led);
    if (l == NULL) {
        return 0;
    }
    return l->period_ms;
}
```

> 注意：`led_on/off` 会停止该 LED 的 blink（确定态覆盖 blink）。

- [ ] **Step 3: 更新 conn_mgr 使用 led 句柄**

`conn_mgr.h`：
- `void conn_mgr_init(led_t led_red, led_t led_blue);`
- `conn_mgr.h` 需 `#include "led.h"`。

`conn_mgr.c`：
- 添加 `static led_t g_led_red; static led_t g_led_blue;`
- `conn_mgr_init(led_t led_red, led_t led_blue)` 存储两个句柄。
- 所有 LED 调用改为句柄版：
  - `conn_enter_fatal`: `led_off(g_led_blue); led_on(g_led_red);`
  - `conn_mgr_tick` 内：`led_blink(g_led_blue, SEARCH_BLINK_MS)` / `led_blink(g_led_blue, RECONNECT_BLINK_MS)` / `led_off(g_led_blue)`
  - 移除 `if (led_is_override()) return;`（Task 2 会重建长按反馈；本 Task 先移除）
- 移除对 `led_btn_feedback` / `led_is_override` 的所有引用。

- [ ] **Step 4: button.c 移除 led_btn_feedback 调用**

`button.c`：删除 `#include "led.h"`，删除 `led_btn_feedback(held * POLL_MS)` 与 `led_btn_feedback(0)` 调用（长按反馈 LED 在 Task 2 由 conn_mgr 回调重建）。

- [ ] **Step 5: main.c 注册 led 句柄**

`main.c`：
- `void axk_main` 内：`led_t led_red = led_init(RED_PIN); led_t led_blue = led_init(BLUE_PIN);`（`RED_PIN = S_MGPIO11`、`BLUE_PIN = S_MGPIO13`）
- `conn_mgr_init(led_red, led_blue);`
- `led_init()` 旧无参调用改为上述新调用。

- [ ] **Step 6: 编译验证**

```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/led-redesign
cmake -S wireless/bs21 -B wireless/bs21/build -DBS21_APP=default
cmake --build wireless/bs21/build -j
```

Expected: 编译成功，0 warning（-Werror）。

- [ ] **Step 7: Commit**

```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/led-redesign
git add wireless/bs21/apps/default
git commit -m "refactor: led module handle-based with software-timer blink"
```

---

### Task 2: button 模块重写（句柄 + 软定时器 + 事件回调）

**Files:**
- Modify: `wireless/bs21/apps/default/button.h`（整体替换）
- Modify: `wireless/bs21/apps/default/button.c`（整体替换）
- Modify: `wireless/bs21/apps/default/conn_mgr.h` / `conn_mgr.c`（接收 button 句柄、注册事件回调、重建长按反馈 LED）
- Modify: `wireless/bs21/apps/default/main.c`（注册 button，去掉 button_task）

**Interfaces:**
- Consumes: Task 1 的 `led_t` / `led_blink` / `led_stop_blinking` / `led_on` / `led_off`。
- Produces（button 模块）:
  - `typedef uint8_t button_t;`
  - `typedef void (*button_down_cb)(void *ctx);`
  - `typedef void (*button_up_cb)(uint32_t held_ms, void *ctx);`
  - `typedef void (*button_hold_cb)(uint32_t held_ms, void *ctx);`
  - `typedef struct { button_down_cb on_down; button_up_cb on_up; button_hold_cb on_hold; } button_callbacks_t;`
  - `button_t button_init(pin_t port);`
  - `void button_set_cb(button_t btn, const button_callbacks_t *cb, void *ctx);`
- Produces（conn_mgr）: `void conn_mgr_init(led_t led_red, led_t led_blue, button_t btn);`

- [ ] **Step 1: 替换 button.h**

`wireless/bs21/apps/default/button.h`：

```c
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
```

- [ ] **Step 2: 替换 button.c**

`wireless/bs21/apps/default/button.c`：

```c
#include "button.h"
#include "gpio.h"
#include "cmsis_os2.h"

#define BTN_MAX 4
#define POLL_MS 10

static const uint32_t HOLD_MARKS_MS[] = { 3000, 10000 };
#define HOLD_MARKS_N (sizeof(HOLD_MARKS_MS) / sizeof(HOLD_MARKS_MS[0]))

#define MS2TICK(ms) \
    ((uint32_t)(((uint64_t)(ms) * osKernelGetTickFreq()) / 1000))

typedef struct {
    bool used;
    pin_t pin;
    osTimerId_t timer;
    uint32_t held_ms;
    bool pressed;
    uint32_t last_hold_idx;
    button_callbacks_t cb;
    void *ctx;
} btn_inst_t;

static btn_inst_t g_btns[BTN_MAX];

static void btn_poll_cb(void *arg)
{
    button_t idx = (button_t)(uintptr_t)arg;
    btn_inst_t *b = &g_btns[idx];
    gpio_level_t level = uapi_gpio_get_val(b->pin);

    if (level == GPIO_LEVEL_LOW) {
        if (!b->pressed) {
            b->pressed = true;
            b->held_ms = 0;
            b->last_hold_idx = 0;
            if (b->cb.on_down != NULL) {
                b->cb.on_down(b->ctx);
            }
        }
        b->held_ms += POLL_MS;
        while (b->last_hold_idx < HOLD_MARKS_N &&
               b->held_ms >= HOLD_MARKS_MS[b->last_hold_idx]) {
            uint32_t mark = HOLD_MARKS_MS[b->last_hold_idx];
            b->last_hold_idx++;
            if (b->cb.on_hold != NULL) {
                b->cb.on_hold(mark, b->ctx);
            }
        }
    } else if (b->pressed) {
        if (b->cb.on_up != NULL) {
            b->cb.on_up(b->held_ms, b->ctx);
        }
        b->pressed = false;
    }
}

button_t button_init(pin_t port)
{
    for (button_t i = 0; i < BTN_MAX; i++) {
        if (g_btns[i].used) {
            continue;
        }
        uapi_pin_set_mode(port, (pin_mode_t)HAL_PIO_FUNC_GPIO);
        uapi_gpio_set_dir(port, GPIO_DIRECTION_INPUT);
        uapi_pin_set_pull(port, PIN_PULL_UP);
        g_btns[i].used = true;
        g_btns[i].pin = port;
        g_btns[i].held_ms = 0;
        g_btns[i].pressed = false;
        g_btns[i].last_hold_idx = 0;
        g_btns[i].timer = osTimerNew(btn_poll_cb, osTimerPeriodic,
                                     (void *)(uintptr_t)i, NULL);
        osTimerStart(g_btns[i].timer, MS2TICK(POLL_MS));
        return i;
    }
    return (button_t)-1;
}

void button_set_cb(button_t btn, const button_callbacks_t *cb, void *ctx)
{
    if (btn >= BTN_MAX || !g_btns[btn].used || cb == NULL) {
        return;
    }
    g_btns[btn].cb = *cb;
    g_btns[btn].ctx = ctx;
}
```

> 说明：`button.c` 不再包含任何 LED 逻辑，只发事件。消抖通过"非按下不计数、按下累计"实现（松开清零，重新按下重新计时）。

- [ ] **Step 3: conn_mgr 接入 button 事件回调并重建长按反馈**

`conn_mgr.h`：
- `void conn_mgr_init(led_t led_red, led_t led_blue, button_t btn);`

`conn_mgr.c`：
- 添加 `static button_t g_btn;`
- `conn_mgr_init` 签名增加 `button_t btn`，存储；并注册回调：

```c
static void on_btn_hold(uint32_t held_ms, void *ctx)
{
    (void)ctx;
    if (held_ms == 3000) {
        led_blink(g_led_blue, 125);
    } else if (held_ms == 10000) {
        led_stop_blinking(g_led_blue);
        led_on(g_led_blue);
    }
}

static void on_btn_up(uint32_t held_ms, void *ctx)
{
    (void)ctx;
    conn_mgr_show_state_led();
    if (held_ms < 3000) {
        conn_mgr_on_short_press();
    } else if (held_ms < 10000) {
        conn_mgr_on_long_press();
    } else {
        conn_mgr_on_very_long_press();
    }
}
```

- 新增 `static void conn_mgr_show_state_led(void)`：按当前状态恢复蓝灯（SEARCH→blink 125、RECONNECT→blink 1000、ACTIVE→off、FATAL 不变）。`on_btn_up` 里在动作前先恢复状态灯。
- `conn_mgr_init` 末尾：

```c
    button_callbacks_t cb = { 0 };
    cb.on_hold = on_btn_hold;
    cb.on_up = on_btn_up;
    button_set_cb(g_btn, &cb, NULL);
```

- `conn_mgr_on_short_press` / `conn_mgr_on_long_press` / `conn_mgr_on_very_long_press` 保持现有逻辑（原本由 button 直接调，现在由 `on_btn_up` 调用）。这些函数可从 `conn_mgr.h` 移除导出（改 static），或在 main.c 不再注册旧回调。

- [ ] **Step 4: main.c 注册 button，去掉 button_task**

`main.c`：
- `button_t btn = button_init(KEY_PIN);`（`KEY_PIN = S_MGPIO0`）
- `conn_mgr_init(led_red, led_blue, btn);`
- 删除 `#include "button.h"` 中的旧回调注册（`button_set_cb(conn_mgr_on_long_press, ...)`）——改为 conn_mgr 内部注册。
- 删除 `create_task(button_task, "button_task")`。
- 若 `button_task`/`button_set_cb` 旧声明不再使用，从 `button.h` 已移除。

- [ ] **Step 5: 编译验证**

```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/led-redesign
cmake --build wireless/bs21/build -j
```

Expected: 编译成功，0 warning。

- [ ] **Step 6: Commit**

```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/led-redesign
git add wireless/bs21/apps/default
git commit -m "refactor: button module handle-based with event callbacks"
```

---

### Task 3: rssi_pick 从 tick 计数改为毫秒时间戳

**Files:**
- Modify: `wireless/bs21/apps/default/rssi_pick.h` / `rssi_pick.c`

**Interfaces:**
- Consumes: `uapi_systick_get_ms()`（`systick.h`）。
- Produces: `bool rssi_pick_feed(const uint8_t addr[6], int8_t rssi);`（签名不变）；删除 `void rssi_pick_tick(void)`。

- [ ] **Step 1: 更新 rssi_pick.h**

`rssi_pick.h`：删除 `void rssi_pick_tick(void);` 声明（其余不变）。

- [ ] **Step 2: 重写 rssi_pick.c 时间基准**

`rssi_pick.c` 关键改动：

```c
#include "rssi_pick.h"
#include "sle_common.h"
#include "systick.h"
#include "string.h"

#define RSSI_THRESHOLD       50
#define RSSI_FILTER_WIN      8
#define RSSI_HOLD_MS         2000
#define RSSI_SWITCH_DB       3
#define RSSI_SWITCH_HOLD_MS  500
#define RSSI_LOST_MS         1000

typedef struct {
    uint8_t  addr[SLE_ADDR_LEN];
    int8_t   hist[RSSI_FILTER_WIN];
    uint8_t  n;
    uint32_t last_seen_ms;
    uint32_t hold_start_ms;
} cand_t;

static cand_t g_cand = { 0 };
static cand_t g_take = { 0 };
static bool g_locked = false;

static uint32_t rssi_now_ms(void)
{
    return (uint32_t)uapi_systick_get_ms();
}
```

- `g_ticks` 删除；`cand_push` 里 `c->last_seen_ticks = g_ticks` 改为 `c->last_seen_ms = rssi_now_ms()`。
- `cand_try_lock` 里 `g_ticks - c->hold_start_ticks >= TICKS_HOLD` 改为 `rssi_now_ms() - c->hold_start_ms >= RSSI_HOLD_MS`；阈值跌落重置 `hold_start_ms = rssi_now_ms()`。
- `rssi_pick_feed` 开头（`g_locked` 检查后）增加 stale recovery（替代原 `rssi_pick_tick`）：
  - 若 `g_cand.n != 0 && rssi_now_ms() - g_cand.last_seen_ms > RSSI_LOST_MS` → 清 `g_cand`。
  - 若 `g_take.n != 0 && rssi_now_ms() - g_take.last_seen_ms > RSSI_SWITCH_HOLD_MS` → 清 `g_take`。
- 新候选/切换候选的 `hold_start` 赋值处改 `= rssi_now_ms()`。
- 切换判定 `g_ticks - g_take.hold_start_ticks >= TICKS_SWITCH` 改 `rssi_now_ms() - g_take.hold_start_ms >= RSSI_SWITCH_HOLD_MS`。
- `rssi_pick_tick()` 函数删除。

> 同时：从 `conn_mgr.c` 的 `conn_mgr_tick` 中删除 `rssi_pick_tick();` 这一行（否则 Task 3 编译失败；`conn_mgr_tick` 其余部分在 Task 4 移除）。

- [ ] **Step 3: 编译验证**

```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/led-redesign
cmake --build wireless/bs21/build -j
```

Expected: 编译成功，0 warning（确认无残留 `rssi_pick_tick` 调用）。

- [ ] **Step 4: Commit**

```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/led-redesign
git add wireless/bs21/apps/default/rssi_pick.c wireless/bs21/apps/default/rssi_pick.h
git commit -m "refactor: rssi_pick uses wall-clock ms instead of tick counter"
```

---

### Task 4: conn_mgr 去 tick（状态转换驱动 LED + 配对超时软定时器）

**Files:**
- Modify: `wireless/bs21/apps/default/conn_mgr.c` / `conn_mgr.h`
- Modify: `wireless/bs21/apps/default/main.c`（去掉 tick_task / scan_task）

**Interfaces:**
- Consumes: Task 1-3（led_t / button_t / rssi_pick_feed 时间戳版）。
- Produces: 删除 `conn_mgr_tick(uint32_t now_ms)`；新增内部状态灯驱动与配对超时软定时器。

- [ ] **Step 1: 状态转换时驱动 LED**

`conn_mgr.c`：
- 新增 `static void conn_apply_state_led(void)`：按 `g_conn_state` 驱动蓝灯：
  - SEARCH → `led_blink(g_led_blue, SEARCH_BLINK_MS)`
  - RECONNECT → `led_blink(g_led_blue, RECONNECT_BLINK_MS)`
  - ACTIVE → `led_off(g_led_blue)`
- 在以下状态改变点调用 `conn_apply_state_led()`：
  - `conn_start_search`（进入 SEARCH）
  - `conn_start_reconnect`（进入 RECONNECT）
  - `conn_mgr_state_changed` 的 CONNECTED 分支（进入 ACTIVE）
- `conn_enter_fatal` 保持 `led_off(g_led_blue); led_on(g_led_red);`。
- 删除 `conn_mgr_tick` 函数内的 LED 驱动逻辑。

- [ ] **Step 2: 配对超时用软定时器**

`conn_mgr.c`：
- 添加 `static osTimerId_t g_pair_timer;`（`#include "cmsis_os2.h"`）。
- 新增超时回调：

```c
static void pair_timeout_cb(void *arg)
{
    (void)arg;
    if (g_conn_state == CONN_STATE_SEARCH && g_search_timeout) {
        osal_printk("[conn] search timeout\r\n");
        conn_exit_search_timeout();
    }
}
```

- `conn_mgr_init` 里创建：`g_pair_timer = osTimerNew(pair_timeout_cb, osTimerOnce, NULL, NULL);`
- `conn_start_search`：若 `timeout`，`osTimerStart(g_pair_timer, MS2TICK(PAIR_TIMEOUT_MS))`；否则 `osTimerStop(g_pair_timer)`。
- `conn_exit_search_timeout` / `conn_start_reconnect` / `conn_mgr_state_changed` CONNECTED / `conn_mgr_on_short_press` 等离开 SEARCH(带 timeout) 时 `osTimerStop(g_pair_timer)`。
- 定义 `MS2TICK` 宏（同 Task 1）。

- [ ] **Step 3: 删除 conn_mgr_tick，移除调用**

- `conn_mgr.h`：删除 `void conn_mgr_tick(uint32_t now_ms);` 声明。
- `main.c`：删除 `tick_task`、`scan_task` 两个函数与其 `create_task(...)` 调用；删除对 `conn_mgr_tick` 的调用；删除 `scan_table_print` 轮询（`conn_mgr_is_scanning`/`scan_table_print` 的周期性打印可由 seek 回调或按键事件替代，或直接删除周期打印）。

- [ ] **Step 4: 编译验证**

```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/led-redesign
cmake --build wireless/bs21/build -j
```

Expected: 编译成功，0 warning（确认无残留 `conn_mgr_tick` / `rssi_pick_tick` / `led_is_override` / `led_btn_feedback` / `create_task` 引用）。

- [ ] **Step 5: Commit**

```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/led-redesign
git add wireless/bs21/apps/default
git commit -m "refactor: conn_mgr event-driven, drop global tick task"
```

---

### Task 5: 编译 + 板上验证

**Files:** 无代码改动（记录结果）。

- [ ] **Step 1: 全量编译**

```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/led-redesign
cmake -S wireless/bs21 -B wireless/bs21/build -DBS21_APP=default
cmake --build wireless/bs21/build -j
```

Expected: 0 warning。

- [ ] **Step 2: 烧录 board_a**

```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/led-redesign
fuser -k /dev/ttyUSB6 2>/dev/null; rm -f /var/lock/LCK..ttyUSB6
python3 wireless/bs21/tools/burn.py board_a wireless/bs21/build/bs21_all_in_one.fwpkg
```

- [ ] **Step 3: 验证状态灯与按键**

按 spec §8 逐项：
1. 无记录上电 → 蓝灯快闪（SEARCH）；连上后灭（ACTIVE）；断开慢闪（RECONNECT）；NV 损坏红灯常亮（FATAL）。
2. SEARCH 态长按 IO0 → 3s 蓝灯转快闪 → 10s 常亮 → 松开恢复状态灯。
3. 短按退出配对、长按进 SEARCH、超长按擦除记录。
4. RSSI 靠近判定、配对超时、断连重连与改前一致。
5. 抓 reset 起 log 验证各事件回调触发顺序。

- [ ] **Step 4: 更新项目文档**

将实测结果写入 `docs/bs21-development.md`（或新增探索记录），说明 LED/button 句柄化与去 tick 的重构结果与验证情况。

```bash
cd /home/bodong/workspace/flydigi-receiver/.worktrees/led-redesign
git add -A
git commit -m "docs: record led/button redesign on-board findings"
```
