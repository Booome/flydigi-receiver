# default app：LED 与 button 模块重构设计（统一设备驱动模式）

日期：2026-08-21
状态：设计定稿
目标固件：`wireless/bs21/apps/default`（正式固件入口，app 名 `flydigi-wireless`）

## 1. 背景与问题

现有 `led.c` / `button.c` 存在职责耦合与接口不清晰：

1. **LED 逻辑与颜色/pin 绑定**：
   - `led_red(bool)` / `led_blue(bool)` 是颜色名接口，接口看不出操作哪个 pin。
   - `led_blink(period_ms)` 硬编码闪蓝灯（`LED_BLUE_PIN`），看不出闪哪个灯。
2. **按键长按 UX 逻辑混入 LED 模块**：
   - `led_btn_feedback(held_ms)` 把按键长按反馈（3000/10000ms 阈值 + override 状态）塞进 LED 模块。
   - `led_is_override()` 泄漏按键反馈内部状态，供 conn_mgr tick 判断是否跳过状态灯驱动。
   - 实际上按键长按反馈是 **button 的消费方（conn_mgr）** 的职责，与 LED 模块无关。
3. **全局轮询 tick**：
   - `conn_mgr_tick`（10ms，由 `tick_task` 驱动）负责：驱动状态灯 blink、`rssi_pick_tick`、配对超时检查。
   - `button_task`（10ms 轮询循环）负责按键采样。
   - 轮询架构导致"blink 靠每 tick 喂一次"的隐式依赖，blink 与状态机强耦合。

## 2. 目标

统一设备驱动模式：**句柄 + 自有软定时器 + 命令/事件**。上层（conn_mgr）通过
命令控制设备、通过事件回调响应，**不再有全局轮询 tick**。

- LED：句柄化、软定时器 blink、纯操作，不含按键/状态机逻辑。
- button：句柄化、软定时器采样、事件回调（down/up/hold），不含 LED/状态逻辑。
- conn_mgr：去 `tick_task`，改事件 + 软定时器驱动；注册 button 回调。
- main.c：去掉 `tick_task` / `button_task` / `scan_task`，改为 init + 注册。

## 3. 软定时器机制（调研结论）

BS21 使用 LiteOS。软件定时器机制为 **swtmr（software timer）**：

- 由**专用 swtmr task 统一驱动**所有软件定时器，多个软定时器共享一个 task，
  **不随数量增加系统负担**。
- SDK 通过 **CMSIS RTOS2** 封装暴露：`osTimerNew(func, type, arg, attr)` /
  `osTimerStart(id, ticks)` / `osTimerStop(id)`，底层由 `libcmsis_user.a` 提供。
- 回调签名 `void (*)(void *arg)`，运行在 **swtmr task 上下文**（非中断），
  回调内可安全操作 GPIO。
- 单位是 **tick**，用 `osKernelGetTickFreq()` 运行时换算 ms→tick。
- 本设计共约 4 个软定时器（LED red/blue blink、button 采样、conn_mgr 配对超时），
  远低于 swtmr 默认上限 16。

## 4. 组件设计

### 4.1 LED 模块（重写 `led.c` / `led.h`）

```c
typedef uint8_t led_t;                     /* LED 句柄（注册序号） */

led_t led_init(pin_t port);                /* 注册一个 LED（GPIO pin，高电平点亮），返回句柄 */
void led_on(led_t led);
void led_off(led_t led);
void led_toggle(led_t led);
void led_blink(led_t led, uint32_t period_ms); /* 启动该 LED 的软定时器，周期翻转 */
void led_stop_blinking(led_t led);
bool led_is_blinking(led_t led);
uint32_t led_get_blink_period(led_t led);  /* 当前 blink 周期；未在 blink 返回 0 */
```

- 每 LED 一个 `osTimerNew` 软定时器；`led_blink` 启动，回调里 `led_toggle`。
- `led_on` / `led_off` 设置确定态；若该灯正在 blink 则先 `osTimerStop`（确定态覆盖 blink）。
- 内部表存：pin、timer id、当前电平、blink 状态与周期。
- `led_get_blink_period` 返回当前周期（供上层互斥/恢复判断）。
- **不含任何按键/状态机逻辑**。

### 4.2 button 模块（重写 `button.c` / `button.h`）

```c
typedef uint8_t button_t;                  /* 按键句柄（注册序号） */

button_t button_init(pin_t port);          /* 注册按键（内部上拉，按下拉低），返回句柄；
                                              内部启动 10ms 采样软定时器 */

typedef void (*button_down_cb)(void *ctx);
typedef void (*button_up_cb)(uint32_t held_ms, void *ctx);    /* 松开，带按压时长 */
typedef void (*button_hold_cb)(uint32_t held_ms, void *ctx);  /* 达到阈值点触发 */

typedef struct {
    button_down_cb on_down;
    button_up_cb   on_up;
    button_hold_cb on_hold;
} button_callbacks_t;

void button_set_cb(button_t btn, const button_callbacks_t *cb, void *ctx);
```

- 内部 10ms 采样软定时器（消抖 + 累计 held）。
- 采样周期：刚按下 → `on_down`；松开 → `on_up(held_ms)`；按住跨过阈值点 → `on_hold(held_ms)`。
- 阈值列表 `HOLD_MARKS_MS[] = {3000, 10000}`，跨过标记点时触发 `on_hold`。
- **不含任何 LED/状态逻辑**——只发事件。
- 长按反馈的 LED 显示由订阅方（conn_mgr 在 `on_hold`/`on_up` 里）驱动。

### 4.3 conn_mgr 重构（去 tick，事件 + 软定时器）

**去掉** `tick_task`（10ms 全局轮询）。**不再每 tick 轮询 blink**（blink 由
LED 模块软定时器自持），改为状态转换时显式驱动：

| 状态 | 蓝灯 | 红灯 |
|------|------|------|
| 进入 SEARCH | `led_blink(blue, 125)` | 灭 |
| 进入 RECONNECT | `led_blink(blue, 1000)` | 灭 |
| 进入 ACTIVE | `led_off(blue)` | 灭 |
| 进入 FATAL | 灭 | `led_on(red)` |

- **RSSI 判定事件驱动**：`seek_result` 回调喂入时用帧时间戳
  （`uapi_systick_get_ms`）替换原 `rssi_pick_tick(10ms)` 推进，滑动窗口按真实时间计算。
- **配对超时用软定时器**：进入 SEARCH(带 timeout) 启动 2min 软定时器，触发时若仍
  在 SEARCH 则退出配对（回 RECONNECT/SEARCH 无 timeout）。
- **注册 button 回调**（长按反馈 + 状态切换）：
  - `on_hold(3000)` → 蓝灯快闪 `led_blink(blue, 125)`（长按反馈开始）
  - `on_hold(10000)` → `led_stop_blinking(blue); led_on(blue)`（常亮，擦除提示）
  - `on_up(held_ms)` → 恢复状态灯；并按 held_ms 判定短按/长按/超长按执行动作
    （退出配对 / 进 SEARCH / 擦除记录）。

### 4.4 main.c 调整

```c
led_t led_red  = led_init(RED_PIN);   /* S_MGPIO11 */
led_t led_blue = led_init(BLUE_PIN);  /* S_MGPIO13 */
button_t btn = button_init(KEY_PIN);  /* S_MGPIO0 */

conn_mgr_init(led_red, led_blue, btn);
conn_mgr_register_button_events();    /* 或 conn_mgr 内部注册 */
enable_sle();
```

去掉 `tick_task` / `button_task` / `scan_task`。`bs21_rst()`、SLE 回调注册、
本地地址设置等保持。

## 5. 互斥问题解决

原 `led_btn_feedback` 的"override"互斥（conn_mgr 状态灯 vs 按键反馈）在新设计下
**自然消除**：按键反馈由 conn_mgr 自己的 `on_hold`/`on_up` 回调驱动，同一时刻
conn_mgr 只在单一回调/状态下驱动蓝灯，无并发竞争，不再需要 override 概念。

## 6. 数据流

```
boot
 └─ main: led_init x2, button_init, conn_mgr_init(led, led, btn)
      ├─ led: 每 LED 一个软定时器；blink 由 osTimer 驱动 toggle
      ├─ button: 10ms 采样软定时器 → 消抖/计时 → on_down/on_hold/on_up 事件
      └─ conn_mgr:
           ├─ 状态转换 → 显式 led_blink/on/off
           ├─ seek_result → 帧时间戳喂 RSSI 判定 → 连接
           └─ 注册 button 回调 → 长按反馈 LED + 状态切换动作
```

## 7. 复用现有实现

- `bs21_util.c`（`bs21_rst`、`sle_setup_set_local_addr`、`sle_scan_start`）保持不变。
- `scan_table`、`conn_nv`、`rssi_pick` 逻辑保留，仅改驱动方式（RSSI 时间戳化）。
- SLE 回调框架（power/enable/seek/connect/pair/auth）保持。
- 移除：`main.c` 的 `tick_task`/`button_task`/`scan_task`；`conn_mgr_tick` 的
  LED 驱动部分（RSSI 时间戳化后 `rssi_pick_tick` 也随之调整）。

## 8. 测试计划（板上）

1. **状态灯**：无记录上电 → 蓝灯快闪（SEARCH）；连上后灭（ACTIVE）；断开后
   慢闪（RECONNECT）；NV 损坏 → 红灯常亮（FATAL）。
2. **长按反馈**：SEARCH 态长按 IO0 → 3s 蓝灯转快闪 → 10s 常亮 → 松开恢复状态灯。
3. **按键动作**：短按退出配对；长按进 SEARCH；超长按擦除记录。
4. **去 tick 后行为回归**：RSSI 靠近判定、配对超时、断连重连与改前一致。
5. 全程抓 reset 起 log 验证各事件回调触发顺序。

## 9. 范围外（YAGNI）

- 不做 LED 亮度/PWM、多色复用。
- 不做 button 长按 repeat（连发）等额外特性。
- IO12 绿灯暂不使用。
- 不改动 `sle_probe`、其他 app、SDK。

## 10. 风险

| 风险 | 应对 |
|------|------|
| osTimer 在 swtmr task 回调操作 GPIO 的并发 | 回调非中断上下文，GPIO 操作安全；单线程回调无竞争 |
| 软定时器数量 | 仅约 4 个，低于 swtmr 默认上限 16 |
| tick→事件化引入 RSSI 判定行为变化 | 保留滑动窗口语义，仅把节拍从固定 tick 改为帧时间戳；板上回归测试验证 |
| blink 周期精度 | 125/1000ms 对精度不敏感，tick 精度足够 |
