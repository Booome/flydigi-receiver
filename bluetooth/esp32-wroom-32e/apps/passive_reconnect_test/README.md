# passive_reconnect_test

最小化被动重连测试 app。无 `dev_open`、无 candidate 逻辑、无 esp_timer。
BTDM + esp_hidh 全开，扫模式 `CONNECTABLE / NON_DISCOVERABLE`，**只等手柄 page 进来**。

## 用途

诊断 Bluedroid 协议栈自身 bonded-peer auto-reconnect 是否能走到 `OPEN_EVENT is_orig=false`。
对照 `apps/default` 场景 2 的 426219 capture（app 侧 lock_tick + bta_hh race → Result:4 拒 CTL
→ 半死 ACL → 内存泄漏 → assert）——本 app 把 app 侧全部动作拿走，看 stack 自己能不能成功。

## 不做什么

- 不主动 `esp_hidh_dev_open`
- 不 inquiry、不扫描
- 不发任何 timer 驱动的连接
- 不删 bond、不写 LST、不操作 link policy

## 怎么跑

```bash
# 编译
python3 bluetooth/esp32-wroom-32e/tools/build.py --app passive_reconnect_test

# 烧录
python3 bluetooth/esp32-wroom-32e/tools/burn.py --board-a --app passive_reconnect_test

# 抓 log
python3 tools/capture_uart.py --board-a --duration 180 --odir /tmp --ts
```

## 测试流程

1. 用 `apps/default` 配对一次（确保 NVS 有 bond）
2. 烧本 app
3. **手柄**：
   - 关机 10s
   - 开机（不要按任何按钮）
4. 等 60s，看 capture 里：
   - 是否有 `ACL_CONN`（pad page 进来）
   - 是否有 `AUTH`
   - 是否有 `OPEN is_orig=0`（stack 内部完成 SDP + HID setup）
   - 是否有 `INPUT` 流

## 判定

| 结果 | 含义 |
|---|---|
| `OPEN is_orig=0 OK` + `INPUT` 流 | stack auto-reconnect 本身可用 → 主 app bug 在应用层 |
| `OPEN is_orig=0 FAIL` + pad 退出 | stack race → Bluedroid 缺陷（与 426219 同源）→ 需绕 bta_hh |
| `ACL_CONN` 后无任何 HID 事件 | stack 不自动起 HID setup → 需 app 主动 open |
| 无 `ACL_CONN` | 手柄根本不发 page  → pad firmware 限制 |

## 已知边界

- 本 app 不实现 30s+ 长 timeout 防呆——capture `--duration` 必须 ≥60s
- 本 app 不做 SDP 主动查询、不重试、不重启——出问题就出问题，不掩盖
- pad 必须先在 `apps/default` 配对成功一次（确保 NVS bonds ≥1）