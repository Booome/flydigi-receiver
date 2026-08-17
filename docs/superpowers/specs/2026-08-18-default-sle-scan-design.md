# default app：SLE 扫描器（识别手柄地址）设计

- 日期：2026-08-18
- 分支：`default-sle-scan`
- 状态：已批准（用户确认）

## 背景

项目主线是开发飞智八爪鱼5 手柄的 SLE 接收器。当前路线图 M5（SLE 扫描验证）已完成：
两块 BS21 Kit 通过 `t_broadcaster`（T 节点广播）+ `g_scanner`（G 节点扫描）验证了 SLE
广播/扫描通信。

下一步（M5 收尾 + M6 铺垫）需要**识别手柄的 SLE 广播地址**，为后续发起连接做准备。
手柄的 SLE 广播行为未知（地址是否随机、广播数据格式、是否接受任意 G 节点连接），
因此第一步通过"开关手柄，观察哪个设备的扫描计数停止增长"来锁定手柄地址。

远期目标（不在本次范围）：做出类似 Xbox 无线接收器的体验——接收器与手柄都进入
"连接模式"，靠近后自动发现、自动配对。

## 目标

- `default` app 使能 SLE 并持续扫描，按地址聚合统计，每 2 秒打印一次设备表。
- 通过开关手柄，观察哪个地址的计数停止增长，从而确定手柄地址。
- 打印格式清晰、不刷屏，便于长时间观察。
- 不包含连接/配对逻辑（下一阶段）。

## 设计

### 1. 功能流程

```
axk_main：打印 app 名 → bs21_rst → 注册设备管理 + seek 回调 → enable_sle()
  ↓
sle_power_on_cb：enable_sle()
  ↓
sle_enable_cb：sle_announce_seek_register_callbacks → 设置 seek 参数 → sle_start_seek()
  ↓
seek_result_cb：按地址更新设备表（命中 count++ 刷新 rssi；未命中新增）
  ↓
统计任务（osal_kthread）：每 2 秒打印一次设备表
```

### 2. 代码结构（`apps/default/main.c`）

复用现有模式（g_scanner / t_broadcaster 的 enable_sle 流程），修改点：

- 删除 `hello_task`（不再需要空循环打印）。
- `seek_result_cb`：聚合逻辑（设备表更新）。
- 新增统计任务：定时打印设备表。

seek 参数沿用 g_scanner 已验证的值：

- `seek_phys = 1`（1M）
- `seek_type[0] = 1`
- `seek_interval[0] = 100`，`seek_window[0] = 100`（连续扫描）
- `filter_duplicates = 0`（不过滤，聚合层按地址合并）

### 3. 设备表

- 静态数组，容量 32 项：`{ sle_addr_t addr; int8_t rssi; uint32_t count; }`
- 线性查找（n ≤ 32，足够）
- 表满：丢弃新设备，打印一次提示（不覆盖，保持简单）
- `seek_result_cb` 运行于 SLE service 线程，必须快速返回（聚合只做内存操作，不打印）

### 4. 打印格式

每 2 秒一次：

```
app: flydigi-wireless
[scan] devices:2
  0) 00:00:00:00:00:00 rssi:-23 cnt:481
  1) 12:34:56:78:9a:bc rssi:-45 cnt:12
```

- 按设备加入顺序输出
- 开关手柄时观察目标地址 `cnt` 是否增长

### 5. 边界与错误处理

- 设备表满：打印 `[scan] table full`，停止新增
- seek 启动失败：打印错误码（`sle_start_seek` 返回值），不崩溃
- SLE 使能失败：打印错误码，保持可观察

## 测试方案

1. **板对板**：board_a 烧 `t_broadcaster`（已知广播源），board_b 烧 `default`。预期 board_b
   打印 `00:00:00:00:00:00` 且 `cnt` 持续增长。
2. **手柄识别**：board_b 跑 `default`，手柄开机（PC 模式）放旁边，观察新增地址；
   手柄关机，该地址 `cnt` 停止增长 → 锁定手柄地址。
3. 记录手柄地址、RSSI、广播数据（广播数据 hex 打印本次不做，后续需要时再加）。

## 后续方向（不在本次范围）

- 连接模式：识别手柄地址后发起 `sle_connect_remote_device`，打印连接/配对状态。
- Xbox 式自动配对机制：进入连接模式后自动发现并配对。
- 数据收发：SSAP 服务发现，NewXInput 协议解析，USB HID 输出。