# capture_uart 串口日志捕获脚本设计

日期：2026-08-27
状态：已批准
相关项目：flydigi-receiver

## 背景

调试 BS21 板（board_a / board_b）时，需要抓取从 reset 起的完整串口日志。
现有手动流程（见 AGENTS.md）用 `stty` + `cat` + `uart-gpio pulse`，繁琐且难以
做时间戳对齐、实时回显与优雅退出。需要一个一次性工具完成「连串口 → 复位 →
持续捕获」的完整流程。

## 目标

提供一个脚本 `wireless/bs21/tools/capture_uart.py`，用法示例：

```bash
python3 wireless/bs21/tools/capture_uart.py --board-a --board-b --rst-a --duration 60 --odir /tmp
```

- 连接所选板子的串口，各输出到一个文件
- 支持延迟复位（复位后能捕获从 boot 开始的完整一轮日志）
- 实时终端回显、到时长自动结束、Ctrl+C 优雅退出

## CLI 接口

```
capture_uart.py [--board-a] [--board-b] [--rst-a] [--rst-b] [--reset-delay N]
                [--duration N] [--odir DIR] [--ts] [--no-echo]
```

| 参数 | 含义 | 默认 |
|---|---|---|
| `--board-a` | 捕获 board_a 串口 | 必选其一 |
| `--board-b` | 捕获 board_b 串口 | 必选其一 |
| `--rst-a` | 复位 board_a（仅当选中 board_a 时有效） | 不复位 |
| `--rst-b` | 复位 board_b（仅当选中 board_b 时有效） | 不复位 |
| `--reset-delay N` | 打开串口后延迟 N 秒再执行复位 | 1 |
| `--duration N` | 捕获时长（秒） | 60 |
| `--odir DIR` | 输出目录 | /tmp |
| `--ts` | 每行加相对时间戳前缀 `[+秒.毫秒] ` | 不加 |
| `--no-echo` | 关闭实时终端回显 | 回显开启 |

校验规则：

- 必须至少选中 `--board-a` 或 `--board-b` 之一，否则报错退出（exit 1）。
- `--rst-a` 仅在 `--board-a` 选中时允许，`--rst-b` 同理；传了未选板子的复位
  参数直接报错退出，避免误复位。
- 复位顺序固定 A → B（与 bs21_connect.py 一致）。

## 串口与复位配置

端口与复位引脚从项目根 `.env` 读取，与现有工具（burn.py / bs21_connect.py）
完全一致：

- board_a 串口：`BS21_BOARD_A_PORT`
- board_a 复位：`BS21_BOARD_A_RST_PORT` + `BS21_BOARD_A_RST_PIN`
- board_b 串口：`BS21_BOARD_B_PORT`
- board_b 复位：`BS21_BOARD_B_RST_PORT` + `BS21_BOARD_B_RST_PIN`

复位执行方式：`subprocess.run(["uart-gpio", "pulse", <rst_port>, "A", <pin>, "0", "2000"])`
（与 burn.py 的 `pulse_reset` 相同）。

## 架构与数据流

单线程 + `select` 多路复用，不引入线程：

1. 解析参数并做校验（至少一块板）。
2. 打开所选串口（pyserial，115200 8N1，timeout=0.05），失败报错 exit 1；
   若多块板中某一块打开失败，提示该板失败并继续捕获其余正常板。
3. 打开失败检查：串口打开前不做强制 kill 端口占用者（capture 是观察工具，
   不应强杀其它会话；若端口被占用则报错提示用户手动释放）。
4. 等待 `--reset-delay` 秒后，按 A→B 顺序执行 `--rst-a` / `--rst-b` 复位。
   复位发生在捕获循环开始后，因此从 boot 起的一轮日志都会落盘。
5. 进入主循环（`select` 轮询两串口，超时 0.05s）：
   - 读到数据 → 实时回显到终端（除非 `--no-echo`）+ 写入对应板子文件。
   - `--ts` 模式下按 `\n` 分界缓存行，完整行写入时加前缀
     `[+秒.毫秒] `（相对捕获起点，便于两板日志对齐）；残留在 buffer 的行尾
     部分在退出时 flush。
6. 退出条件：
   - 捕获时长达到 `--duration` → flush 残行、关闭文件、打印汇总、exit 0。
   - Ctrl+C（SIGINT）→ flush 残行、关闭文件、打印汇总、exit 130。
7. 任一板子串口读取异常（非 Ctrl+C）→ 记录错误，若该板已无数据则跳过，
   其余板继续捕获；不影响整体退出逻辑。

## 文件输出

- 命名：`board_a_<YYYYmmdd_HHMMSS>.log` / `board_b_<YYYYmmdd_HHMMSS>.log`，
  时间戳为捕获开始时刻。
- 写入二进制模式（raw bytes）。`--ts` 时写入的是带时间戳前缀的字节流
  （时间戳前缀本身为 ASCII）。
- 输出到 `--odir`（不存在则创建）。
- 结束汇总打印：每个文件路径 + 字节数 + 捕获时长。

## 测试

1. **虚拟串口对**：用两块 `socat` 虚拟串口对（`pty,raw,echo=0`）模拟 board_a/
   board_b，配合 uart-gpio 不可用时的打桩，验证：
   - 纯 raw 捕获：两板输出各自落盘、实时回显。
   - `--ts` 时间戳前缀正确、行残留在退出时 flush。
   - Ctrl+C 优雅退出，文件已保存。
   - 只选一块板；两块板都选。
   - 参数校验：都不选 / 未选板却传复位参数 → 报错。
2. **真实板验证**：用 `uart-gpio pulse` 复位 board_a，确认能捕获从 `boot.`
   → `Flashboot Init!` → 应用起点的一轮完整启动日志。