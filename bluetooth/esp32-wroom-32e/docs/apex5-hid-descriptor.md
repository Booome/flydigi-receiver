# Apex5 BT HID Report Descriptor（Xbox Wireless Controller 模式）

> 本次实测手柄身份：`Xbox Wireless Controller` @ `b5:5d:e7:98:54:75`（PC>Bluetooth / X-input 模式）。
> 另一身份 `Pro Controller` @ `b5:5d:e9:98:54:75`（Switch 模式）下，`esp_hidh_dev_open` 失败（`status=0xffffffff`，Switch 模式伪装成 `057e:2009` 走不同协议/handshake）。本次 dump 仅 Xbox 模式；Pro 模式 dump 待能在 Pro 身份下成功连接时补。

## 抓取记录

- 时间：2026-09-05（本次实验，dump 在 `esp_hidh_dev_open` 成功后由 `hid_dump_report_map()` 打印）
- 工具：`apps/default/` 配对并连接后自动 dump
- 原始 hex：见同目录 `apex5-hid-descriptor.hex`（306 字节，20 行）

## esp_hid_parse_report_map 报告元数据

```
[hid] descriptor[0] len=306 bytes:
[hid] report: map=0 id=1 type=INPUT  usage=GAMEPAD   len=15   ← 本里程碑解码目标
[hid] report: map=0 id=2 type=INPUT  usage=GENERIC   len=1
[hid] report: map=0 id=3 type=OUTPUT usage=GENERIC   len=8
[hid] report: map=0 id=4 type=INPUT  usage=GENERIC   len=1
```

- 报告 #1：`INPUT`/`GAMEPAD`/15 字节 → 与 Xbox 360 有线 XInput 报告长度一致。
- 报告 #3：`OUTPUT`/`GENERIC`/8 字节 → 震动 (rumble) 输出报告。
- 报告 #2、#4：`INPUT`/`GENERIC`/1 字节（LED/连接状态反馈）。

## 字节↔字段映射

**Task 3 收尾时通过逐键实测填表**（不在此处先猜）。已抽取的对照样本：

- 空闲帧（无按键无摇杆）:
  - `[hid] report: ... data=0081ff7f0080ff7f00000000000000`（多次）
  - 字节 1=`0x81`、字节 2=`0xff`、字节 4=`0x80`、字节 5=`0xff`、
    字节 6/8/10/12=`0x00`、`0x7f`、`0x80`、`0x00`、14=`0x00`。
- 注意：Xbox 360 XInput 报告通常 byte0=report-id=0x00, bytes 1–2=buttons16, bytes 6–13=4 路 16-bit 模拟轴;
  但 byte1=`0x81`、byte2=`0xff` 在完全空闲态不该全 1（疑似手柄在 dump 后的「连接/握手瞬态」残留位），
  → Task 3 实测后以真按钮映射为准。
- **不要**在此处先入为主写「byte1=0x10=START」之类，参见 `apex5-hid-input-map.md` 的 Task 3 实测结论。

## 解析建议

306 字节完整 Report Descriptor 公开、可查「Xbox 360 wired controller HID Report Descriptor」参考，
但防掩耳盗铃原则：字段→位的最终映射以**实测（按下每个按钮+每轴）**为准，
见 `apex5-hid-input-map.md`（Task 3 产出）。

## 已记录问题（需在固件自动处理）

- `esp_hidh_dev_open` 对 stale bond 返回 `status=0xffffffff`（见本次 + 历史多次），手柄
  factory reset 后 ESP32 NVS 里仍留旧 bond → 鉴权失败。固件需**自动清除/覆写旧 bond**
  而不是依赖手工 `esptool erase_flash`。具体方案另列任务。