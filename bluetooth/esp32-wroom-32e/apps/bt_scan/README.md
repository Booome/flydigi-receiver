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
[bt_scan] mode=BR_EDR addr=AA:BB:CC:DD:EE:FF name="Flydigi Apex5" cod=0x002504 rssi=-55
[bt_scan] mode=BLE    addr=11:22:33:44:55:66 name="Xbox Wireless Controller" rssi=-67
```

`mode`: `BR_EDR` 或 `BLE`
`addr`: 6 字节 MAC
`name`: 设备名（UTF-8，可能为空）
`cod`: Class of Device（24-bit hex，仅 BR/EDR 有效；BLE 通常 0）
`rssi`: 信号强度 dBm

## 验证步骤（见 spec 第十节）

1. build 成功
2. burn 烧 board_a 成功
3. capture_uart 看到启动 banner + 至少 1 轮扫描
4. 手柄切到 BT 模式：双侧命中（BR/EDR `Flydigi Apex5` + BLE `Xbox Wireless Controller`）
5. 对照设备（手机 / 电脑）开着蓝牙 → 至少一侧命中（验证双模鲁棒）