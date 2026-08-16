# M5: SLE 扫描验证设计

## 一、背景与目标

P0 已打通编译 → 烧录 → 启动 → 串口打印全链路，当前 `app` 组件（`axk_main` + hello_task）
作为入口正常运行。下一步 M5 的目标是**确认 BS21 能扫描到手柄的 SLE 广播**。

**M5 成功标准**：扫描到手柄 SLE 广播，获取其地址、广播数据、RSSI。

**范围**（只做扫描验证，不做连接/配对/数据解析）：

1. 编译系统支持多工程（一个 target 编译出多个应用固件）。
2. 新建两个测试工程并入库：`g_scanner`（G 节点扫描器）+ `t_broadcaster`（T 节点广播器）。
3. 两块 Kit 互通验证（G 扫到 T 广播）。
4. 扫描手柄，验证 M5 成功标准。

**明确不做**：连接/配对（M6）、数据解析（M7）、USB HID 输出（M8）。

## 二、关键调查结论

- **`bs21-n1100-rcu` 默认是 T 节点（peripheral）**：其 `config.py` defines 为
  `['SUPPORT_SLE_PERIPHERAL', 'CONFIG_BT_SLE_ONLY']`。peripheral 只能广播，不能扫描。
- **扫描需要 G 节点（central）**：defines 需为 `SUPPORT_SLE_CENTRAL`。
- **SDK 有 central 预编译库、无 central application target**：`bs21-n1100-sle-central/`
  目录下 bth_sdk/bt_host/bg_common/bgtp/bth_gle 全套库齐全，但 `config.py` 只有
  `standard-bs21-n1100` 与 `bs21-n1100-rcu` 两个 application target（均 peripheral）。
  因此 central 需在 `gen-config.py` 里自行切换（defines + 库目录），不改 SDK。
- **库目录选择逻辑**（`protocol/bt/controller/bgtp/bs21.cmake`）：当定义了
  `CONFIG_SLE_BLE_SUPPORT` 且 `SUPPORT_MULTI_LIBS` 在 defines 中时，库取
  `${PKG_CHIP}-${CONFIG_SLE_BLE_SUPPORT}/lib*.a`。故定义 `CONFIG_SLE_BLE_SUPPORT=sle-central`
  即可让蓝牙组件链接 `bs21-n1100-sle-central/` 的库；`SUPPORT_MULTI_LIBS` 已由
  `standard-bs21-n1100` 的 defines 提供。
- **SDK 例程不能直接 build.py 运行**：`bs21-n1100-rcu` 默认例程是 `CONFIG_SAMPLE_SUPPORT_RCU=y`
  （遥控器），`SAMPLE_SUPPORT_SLE_UART` 默认未启用，且 central 需额外切换。故放弃
  "先跑 SDK 例程验证"，改为直接移植例程逻辑到自有工程。

## 三、目录结构（多工程）

```
wireless/bs21/
├── CMakeLists.txt            # 顶层入口，接收 -DBS21_APP= 选择 apps/ 下的工程
├── apps/
│   ├── g_scanner/            # 工程1：G 节点扫描器（central）
│   │   ├── CMakeLists.txt
│   │   └── main.c            # axk_main → SLE 扫描
│   └── t_broadcaster/        # 工程2：T 节点广播器（peripheral）
│       ├── CMakeLists.txt
│       └── main.c            # axk_main → SLE 广播
├── sdk-compat/               # 不变（ble_stub.c，补齐 SLE-only 库残留的 sapi_ble_* 符号，按需）
├── scripts/
│   ├── gen-config.py         # 增加 BS21_APP 参数，按工程切角色 + 注册组件
│   └── setup-sdk.sh          # 不变
└── src/
    └── controller_state.h    # 不变
```

删除：`app/`（hello_task 演示使命完成，被 `apps/` 替代）。

## 四、构建流程（多工程 + 角色切换）

```bash
# 工程1：G 节点扫描器（central 库）
cmake -S wireless/bs21 -B output/g_scanner -DBS21_APP=g_scanner
cmake --build output/g_scanner -j

# 工程2：T 节点广播器（peripheral 库，默认）
cmake -S wireless/bs21 -B output/t_broadcaster -DBS21_APP=t_broadcaster
cmake --build output/t_broadcaster -j
```

`gen-config.py` 按 `BS21_APP` 处理：

- 注册组件：`env.append('ram_component', BS21_APP)`（`g_scanner` 或 `t_broadcaster`）。
- 角色切换（仅 `g_scanner`）：defines 中 `SUPPORT_SLE_PERIPHERAL` → `SUPPORT_SLE_CENTRAL`，
  并定义 `CONFIG_SLE_BLE_SUPPORT=sle-central` 使库链接 central 版。
- `t_broadcaster` 保持默认 peripheral 配置（现有 `bs21-n1100-rcu` 库）。

顶层 `CMakeLists.txt` 按 `BS21_APP` 执行 `add_subdirectory(apps/${BS21_APP})`，
并给 `gen-config.py` 传 `BS21_APP`。输出目录分离，两工程可独立编译烧录。

## 五、数据流

**g_scanner（G 节点扫描器，移植自 `sle_uart_client.c` 扫描部分）**：

```
axk_main → sle_central_init()
  → 注册 device 回调 → enable_sle()
  → sle_enable_cb → sle_set_seek_param() + sle_start_seek()
  → seek_result_cb: 打印每个广播 addr + data + rssi（持续扫描，不匹配 name，不停止）
```

**t_broadcaster（T 节点广播器，移植自 `sle_uart_server_adv.c` 广播部分）**：

```
axk_main → sle_broadcast_init()
  → 注册 device 回调 → enable_sle()
  → sle_enable_cb → sle_set_announce_param() + sle_set_announce_data() + sle_start_announce()
  → 持续广播（固定 name + CONNECTABLE_SCANABLE 模式）
```

扫描器打印所有广播（不按 name 过滤），因为手柄广播内容未知；手柄地址、广播数据、RSSI
全部从 `seek_result_cb` 的参数读取。

## 六、分步实施

1. **gen-config.py 改造**：新增 `BS21_APP` 参数，注册对应组件；`g_scanner` 时切
   defines + 库目录到 central。
2. **顶层 CMakeLists 改造**：接收 `-DBS21_APP=...`，`add_subdirectory(apps/${BS21_APP})`。
3. **重构**：`app/` → `apps/g_scanner/` + `apps/t_broadcaster/`（各写 `main.c` 移植例程逻辑）。
4. **编译两工程**：验证 central / peripheral 库都能正确链接（central 库符号差异如有
   sapi_ble_* 残留，按需调整 `sdk-compat`）。
5. **烧录两块 Kit**：互通验证（g_scanner 串口打印 t_broadcaster 的广播）。
6. **扫描手柄**：手柄开机（2.4G 模式），g_scanner 打印手柄广播，达成 M5 成功标准。

## 七、错误处理

- 各回调打印状态码：SLE power on / enable 失败、seek/announce enable 失败等。
- 扫描/广播启动失败时打印错误码并保持可观察（不静默失败）。

## 八、测试与成功标准

- **互通测试**：两块 Kit 分别烧 `g_scanner` / `t_broadcaster`，`g_scanner` 串口应打印
  `t_broadcaster` 的广播（地址 + name + RSSI）。
- **手柄扫描测试**：手柄开机（2.4G 模式），`g_scanner` 打印手柄广播 → 拿到地址 +
  广播数据 + RSSI。
- **成功标准**：扫描到手柄 SLE 广播，获取地址和广播数据。
