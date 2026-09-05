# bt_scan (M10 second app)

ESP-IDF `examples/bluetooth/esp_hid_host` 精简版：去掉自动连接 / state machine，
保留 BR/EDR + BLE 双模扫描循环，每轮 5s。

源来自 `/opt/esp-idf/examples/bluetooth/esp_hid_host/`（AUR 包内）；
本目录是从该 example `cp -r` 后删 `esp_hid_host_main.c`、改写为 `main.c`（自定义 scan 循环 + printf 打印）
得到的本地副本。ESP-IDF 保持只读。

## 构建 + 烧录

```bash
source /opt/esp-idf/export.sh
cd bluetooth/esp32-wroom-32e
python3 tools/build.py --app bt_scan
python3 tools/burn.py --app bt_scan    # 烧 board_a（默认 BOARD_A_PORT）
```

## 串口输出格式（一行一条命中）

```
[bt_scan] mode=BR_EDR addr=AA:BB:CC:DD:EE:FF name="Xbox Wireless Controller" cod(major=5 minor=2 srv=0x000) rssi=-55
[bt_scan] mode=BLE    addr=11:22:33:44:55:66 name="U-ACGDDEC" appearance=0x0000 rssi=-67
```

`mode`: `BR_EDR` 或 `BLE`
`addr`: 6 字节 MAC
`name`: 设备名（UTF-8，可能为空）
`cod`: 仅 BR/EDR：`major=主类 minor=副类 srv=服务类位掩码`（十六进制，无补 0）
`appearance`: 仅 BLE：GAP Appearance 16-bit 值
`rssi`: 信号强度 dBm

## 验证步骤（见 spec 第十节）

1. build 成功
2. burn 烧 board_a 成功
3. capture_uart 看到启动 banner + 至少 1 轮扫描
4. **手柄验证（实测 2026-09-05）**：手柄切到 BT 模式 + 进配对，ESP32 扫到 BR/EDR 命中 → 见下方"验证结果"
5. 对照设备（手机 / 电脑）开着蓝牙 → 验证双模（BR/EDR 看到对照设备的经典蓝牙广播）

## 验证结果（2026-09-05 飞智八爪鱼5 实测）

测试环境：
- ESP32 DevKitC (board_a, `BOARD_A_TYPE=esp32-wroom-32e`，DTR 复位)
- 手柄：飞智八爪鱼5，拨杆到中间（蓝牙模式，蓝色 LED 亮），进入配对状态
- `apps/bt_scan` 跑在 board_a，40s 持续扫描（每 5s 一轮）

手柄 BR/EDR inquiry 命中（节选）：
```
BT : b5:5d:e7:98:54:75, COD: major: PERIPHERAL, minor: 2, service: 0x000, RSSI: -50, NAME: Xbox Wireless Controller
[bt_scan] mode=BR_EDR addr=b5:5d:e7:98:54:75 name="Xbox Wireless Controller" cod(major=5 minor=2 srv=0x000) rssi=-50
```

| 字段 | 值 |
|---|---|
| MAC | `b5:5d:e7:98:54:75` |
| NAME | `Xbox Wireless Controller` |
| COD | major = PERIPHERAL (5)，minor = 2（gamepad/joystick） |
| UUID | 0x1124（HID over BR/EDR L2CAP） |
| RSSI | -48 ~ -58 dBm |
| 40s 内命中次数 | ~60 次 BR/EDR hits |

**结论**：
1. 手柄在 BT 模式下走 **BR/EDR 经典蓝牙**，BLE 不广播
2. 手柄广播的 device name 是 **`Xbox Wireless Controller`**（不是 "Flydigi Apex5"），这是手柄 BP1Y303-D4 把标准 HID-over-BR/EDR 映射成 Xbox 兼容身份
3. esp_hid_scan 的 `num_results` 只对 HID 匹配设备返回非 0；普通 BLE 设备（如 Apple `U-ACGxxxx`）会被 GAP debug print 但不进 results 列表（这是 esp_hid 例子的设计，不是 bug）

**BLE 侧 0 命中**：手柄在 BT 模式下不广播 BLE。说明手柄是**单模 BR/EDR 蓝牙**（不是 BR/EDR + BLE 双模），纠正了原先 docs/controller-modes.md 里"一部分走 BLE、一部分走经典蓝牙"的猜测。