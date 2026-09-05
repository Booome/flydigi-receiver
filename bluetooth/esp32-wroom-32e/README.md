# ESP32-WROOM-32E（蓝牙方向）

飞智八爪鱼5 自制接收器项目的 **蓝牙方向**：用 ESP32-WROOM-32E 研究手柄的
**蓝牙（BR/EDR）** 模式。与 SLE 方向（`wireless/`）完全独立。

- 目标芯片 ESP32-WROOM-32E（ESP32-D0WD-V3，Xtensa LX6 双核 240MHz，rev3.1）
- **唯一**在 ESP-IDF 家族里带经典蓝牙 BR/EDR 的 SoC（S2/S3/C3/C5/C6/H2/C61/E22 全 BLE-only），
  是 BR/EDR HID 主机研究的对口选择
- 框架 ESP-IDF v6.0.2（yay AUR `esp-idf`，只读装在 `/opt/esp-idf`）
- 非侵入式：example `cp -r` 进 `apps/` 再改；编译产物落 `build/<app>/`

## App 一览

**主功能固定在 `apps/default/`**，后续迭代都更新它，不新建 app。`hello_world`/`bt_scan` 是历史基线，保留不再动。

| app | 作用 | 状态 |
|---|---|---|
| `apps/hello_world/` | 环境基线（编译/烧录/串口 hello world 验证） | 稳定 |
| `apps/bt_scan/` | BR/EDR + BLE 双模扫描，看到手柄广播 | 稳定 |
| `apps/default/` | **项目主 app**：BR/EDR HID 主机连接手柄 → 采集/解码 HID 输入报告 | 迭代中 |

`apps/default/` 里程碑：
1. 三层候选算法连上飞智八爪鱼5，采集 15 字节原始 HID 报告（hex 打串口）✓
2. dump/解析 Report Descriptor + struct 解码 + 逐键实测映射 → 进行中

## 快速上手

```bash
source /opt/esp-idf/export.sh
cd bluetooth/esp32-wroom-32e
python3 tools/build.py            # 默认 app=default
python3 tools/build.py --app hello_world    # 也可指定历史 app
python3 tools/burn.py             # 默认 app=default，烧 board_a（需连接 DevKitC）

# 抓串口 log（顶层共享工具；ESP32 需 .env 里 BOARD_A_TYPE=esp32-wroom-32e → DTR 复位）
python3 ../../tools/capture_uart.py --board-a --rst-a --duration 10 --odir /tmp --ts
```

> 烧录前确认手柄：背面拨杆到中间（蓝牙/蓝色 LED）、长按配对键进可发现（蓝灯快闪）。
> 手柄空闲 10–30s 即停止广播（省电），采集前务必刚操作完。

## 项目布局

```
bluetooth/esp32-wroom-32e/
├── apps/                # 每个 app 一个独立 ESP-IDF 项目
│   ├── hello_world/     # 环境基线
│   ├── bt_scan/         # 双模扫描工具
│   └── default/         # 主 app：HID 主机连接 + 报告解码
├── components/          # 跨 app 共享的 ESP-IDF 组件（自动发现）
├── build/               # 编译产物（不入库）
├── docs/                # 平台文档（descriptor/映射表、开发笔记、样例）
└── tools/
    ├── build.py         # idf.py build 包装（--app，默认 default）
    └── burn.py          # idf.py flash 包装（--app，默认 default；端口读 .env）
```

工具链跨方向共享：串口抓取走顶层 `tools/capture_uart.py`，`.env` 端口键
（`BOARD_A_PORT`/`BOARD_B_PORT`）SLE 与蓝牙复用；`BOARD_<X>_TYPE` 选复位方式
（`esp32-wroom-32e` → DTR，SLE 板 → `uart-gpio`）。

## 详细文档

- 平台开发笔记：`docs/development.md`
- 蓝牙模式协议/广播：`docs/controller-modes.md`、`docs/apex5-hid-descriptor.md`、`docs/apex5-hid-input-map.md`
- 设计/计划：`docs/superpowers/{specs,plans}/2026-09-05-*`