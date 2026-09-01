# 项目历史（尝试记录）

本文档记录项目的技术演进与尝试过程，保留历史脉络。代码与文档中与 nRF52840 相关的内容已于 2026-08 清理，仅在此留下记录。

## 阶段 1：nRF52840 BLE 尝试（搁置）

最初假设手柄使用 BLE HID over GATT，计划用 nRF52840 Dongle 做 BLE 接收器。

- 完成 M0-M3 输出管道基础设施：USB CDC ACM 双串口（Debug + Data）、文本/二进制格式化器（含单元测试）、UART TTL 输出后端
- 后经实测确认手柄蓝牙模式实际使用 **BR/EDR（经典蓝牙）**，而非 BLE，BLE 方向搁置

## 阶段 2：nRF52840 2.4GHz 无线尝试（失败）

假设手柄 2.4GHz 模式使用 Nordic ESB 或类似私有协议，尝试用 nRF52840 radio 空中抓包。

- 经 FCC 认证文件（FCC ID: 2AORE-K5）与内部芯片识别确认：手柄 2.4GHz 使用**星闪 SLE 1.0（NearLink）**
- SLE 的 PHY 层（Polar 码编码、中心调度、特定帧格式）与 Nordic radio 不兼容
- nRF52840 radio 只能解调 Nordic 兼容的 GFSK 信号，无法接收 SLE 信号，此路不通

详细分析见 `docs/sle-analysis.md` 第二节。

## 阶段 3：BS21 星闪开发（当前）

确认手柄 SLE 芯片为 P352903N1（星闪 SLE 1.0，飞智定制编号）。
  **注意**：P352903N1 的具体芯片型号无法从公开渠道验证，可能基于海思 Hi2821 (BS21E) 平台。

- 采购 Ai-BS21-32S-Kit（2 块，基于 Hi2821/BS21E）
- SDK 选型：先后尝试 fbb_bs2x 与 Ai-BS21_SDK
  - fbb_bs2x（海思官方）：闭源 loaderboot 不支持安信可板子 SiP flash（报 `0x80001341`），无法烧录运行，放弃
  - **Ai-BS21_SDK（选用）**：`bs21-n1100-rcu`（SLE-only）target + `bs21-rcu` 分区表，512KB flash 可容纳 SLE，overlay 模式开发
- 烧录基于社区 ws63flash 适配（官方 BurnTool 仅 Windows）
- **烧录链路已打通（2026-08-15 实测）**：ws63flash 无需改代码即可烧录 BS21，
  握手（`0xf0`）+ ymodem 传输协议与 WS63 兼容；唯高波特率 921600 在 CH340 串口
  上不稳定，实测 **460800 稳定**。已成功完整烧录安信可 `init_sdk_fw.fwpkg`
  （6 镜像全 100%，含 loaderboot/partition/flashboot A+B/application/nv）。
- **P0 完成（2026-08-15）**：编译 → 烧录 → 启动 → 串口打印全链路打通。修复
  `bs21-n1100-rcu` flash 布局 bug（partition 表 application @ 0xb000，但链接脚本
  按 standard 布局 @ 0x15000，需补 `NO_BOOT_BACKUP`）。
- **构建流程对齐（2026-08-16）**：CMake 构建产物曾 `boot.` 循环，根因是 sign 后缺
  `objcopy --enable_sec` 追加的 64 字节 sec 信息（flashboot 校验失败）。补上
  `GENERAT_SEC_IMAGE` 后 CMake 产物与 SDK `build.py` 逐字节一致。
- **SDK 只读收尾（2026-08-16）**：ble_stub 从 SDK `standard_porting` overlay 迁回
  `sdk-compat`；`app` 组件替换 SDK `demo` 作为入口（`axk_main` + `hello_task`）；
  SDK 树保持干净（仅 `setup-sdk.sh` 的 chmod + LiteOS `.a` 软链）。
- **断连检测验证（2026-08-19）**：确认 SLE 对端断电可感知（supervision，
  `disc:0x7` 链路超时）。此前"真断电 90s 不感知 / 手柄 3 分钟不感知"均为假象——
  Ai-BS21-32S-Kit 模块 reset 引脚外接控制板 GPIO 默认 HIGH，拔 USB 后灌电维持
  模块运行。拔电后主动将 reset 写 0 切断灌电，对端按 superv 超时感知
  （superv=200 时约 2s）。手柄实测：接收器断电（切断灌电）→ 手柄约 2s 感知，
  与官方 Dongle 场景一致。另修复配对失败根因（`auth_complete_cb` 未注册致 SMP
   密钥未保存）。详见 `wireless/ai-bs21-32s-kit/docs/development.md` M6.5。
- **重连修复（2026-08-19）**：`sle_accept` 断开后停广播（re-announce 曾回退）导致
  对端（G）重新上电后扫不到 T、无法重连。修复：断开且非本端主动时延迟 5s 重新
  announce。验证：A 断电 → B 约 2s 感知 → A 上电自动重连成功；"先复位 B、2s 后
  复位 A"顺序也连接成功（此前失败）。详见 `wireless/ai-bs21-32s-kit/docs/development.md` M6.5。
- **default app 连接管理/配对逻辑（2026-08-20）**：完成正式固件的连接管理：
  连接状态机（RECONNECT/SEARCH/PAIR/ACTIVE，断开统一回 RECONNECT）、IO0 按键
  （长按 3s 进配对、短按退出）、LED（红 IO11 严重错误常亮 / 蓝 IO13 配对闪烁）、
  RSSI 就近选择（滑动滤波 + 持续保持 2s + 滞后抢占 + 失联宽限，阈值待标定）、
  NV 记录存储（key 0x3001，无记录正常搜索，真读失败重试后红灯）。全 app
  clean 编译零 warning/error。详见 `wireless/ai-bs21-32s-kit/docs/development.md` M6.6。

详见 `wireless/ai-bs21-32s-kit/docs/development.md`。

## nRF52840 成果保留说明

nRF52840 阶段（2025 初）的代码虽已清理，但其设计成果被 BS21 项目继承：

| 成果 | 去向 |
|------|------|
| `controller_state.h` 数据结构定义 | 迁移至 `wireless/ai-bs21-32s-kit/src/controller_state.h`（去掉 Zephyr 依赖） |
| formatter 文本/二进制格式化设计思路 | BS21 侧重新实现（见实施计划 P3） |
| USB CDC 输出管道设计 | BS21 USB 2.0 CDC 重新实现 |
| 串口接口字符串识别方案（sysfs） | 后续 BS21 USB 输出阶段复用 |
