# ESP32-WROOM-32E（蓝牙方向，M9）

飞智八爪鱼5 自制接收器项目中的 **蓝牙方向** 第一里程碑：
搭建 ESP32-WROOM-32E 开发环境，为后续 BR/EDR HID 主机研究
（连接手柄的蓝牙模式，详 `docs/controller-modes.md`）做准备。

**本里程碑只做环境**：hello_world 编译 + 烧录 + 串口输出。
**不**涉及手柄或蓝牙协议。

## 快速上手

```bash
source /opt/esp-idf/export.sh
cd bluetooth/esp32-wroom-32e
python3 tools/build.py            # 默认 hello_world app
python3 tools/burn.py             # 默认 hello_world，需连接 DevKitC
python3 ../../tools/capture_uart.py --board-a --duration 10 --odir /tmp --ts
```

详细见 `docs/development.md`。

## 项目布局

```
bluetooth/esp32-wroom-32e/
├── apps/                # 每个 app 一个独立 ESP-IDF 项目
│   └── hello_world/
├── components/          # 跨 app 共享的组件（ESP-IDF 自动发现，M9 暂空）
├── build/               # 编译产物（不入库）
└── tools/
    ├── build.py
    └── burn.py
```