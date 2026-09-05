# default (M11 main app)

飞智八爪鱼5 自制接收器项目的**主 app**。从本里程碑起，所有蓝牙方向的功能迭代都更新这里，不新建其他 app。

当前实现（Task 1 占位）：BTDM 初始化后空跑。完整三层候选算法 + HID 报告采集在 Task 2+。

## 构建 + 烧录

```bash
source /opt/esp-idf/export.sh
# tools/build.py / burn.py 默认 app=default，无需 --app
python3 bluetooth/esp32-wroom-32e/tools/build.py
python3 bluetooth/esp32-wroom-32e/tools/burn.py
# 抓 log（DTR 复位，沿用 M10 .env 配置）
python3 tools/capture_uart.py --board-a --rst-a --duration 60 --odir /tmp --ts
```

## 验证步骤

详见 `docs/superpowers/specs/2026-09-05-bt-hid-host-capture-design.md` §十。