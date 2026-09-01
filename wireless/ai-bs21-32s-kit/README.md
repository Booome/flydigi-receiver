# Ai-BS21-32S-Kit (BS21)

基于 Hi2821 (BS21，海思型号名 BS21E) 芯片的 SLE 接收器开发平台。

> **状态**：已挂起。因 SDK 功能限制（缺少关键 API、协议栈闭源、部分发现类型
> 不支持），开发受阻，后期可能废弃。硬件保留，固件和逆向实验不再活跃开发。
> 新主平台已切换到 BearPi-Pico H3863。

## 芯片规格

- SLE 1.0 + BLE 5.4 + USB 2.0
- 双 Type-C：USB1 原生 USB 2.0（HID/CDC），USB2 CH340 串口（烧录/调试）

## SDK

安信可 **Ai-BS21_SDK**（`~/.local/Ai-BS21_SDK`），只读引用模式（SDK 不修改源码）。

## 目录结构

```
ai-bs21-32s-kit/
├── CMakeLists.txt          # 项目根 CMake（SDK 参数 + 构建规则）
├── toolchain.cmake         # 工具链配置
├── apps/
│   ├── default/            # 默认 SLE 接收器固件
│   ├── flydigi_decoy/      # 手柄侧 server（镜像手柄属性表）
│   ├── g_scanner/          # 通用扫描器
│   ├── sle_accept/         # SLE 接受实验
│   ├── sle_connect/        # SLE 连接实验
│   ├── sle_pair/           # SLE 配对实验
│   ├── sle_probe/          # SLE probe（client，扫描/连接/发现/读写）
│   └── t_broadcaster/      # 广播实验
├── src/
│   ├── bs21_util.c/h       # 平台工具函数
│   ├── scan_table.c/h      # 扫描表
│   └── controller_state.h  # 手柄状态定义
├── scripts/
│   ├── setup-sdk.sh        # SDK 准备（恢复 exec 位、symlink 缺失 lib）
│   └── gen-config.py       # 生成 sdk-config.cmake
├── sdk-compat/
│   ├── ble_stub.c          # BLE stub
│   └── los_memfree_wrap.c  # LiteOS memfree 包装
└── tools/
    ├── ble_probe.py        # BLE 探测
    ├── bs21_connect.py     # 连接工具
    ├── bs21_disconnect_test.py
    ├── patch_gle_decoy.py  # 协议栈库 patch（decoy 用）
    └── regress_find.py     # 回归测试
```

## 构建

```bash
# 一次性前置
bash wireless/ai-bs21-32s-kit/scripts/setup-sdk.sh

# 配置 + 构建（BS21_APP 选择 app，默认 default）
cmake -S wireless/ai-bs21-32s-kit -B wireless/ai-bs21-32s-kit/build -DBS21_APP=sle_probe
cmake --build wireless/ai-bs21-32s-kit/build -j
```

产物：`wireless/ai-bs21-32s-kit/build/<app>/bs21_all_in_one.fwpkg`

## 烧录与抓 log

```bash
# 烧录（共享 burn.py）
python3 wireless/tools/burn.py board_a                          # default app
python3 wireless/tools/burn.py board_a -a sle_probe             # 指定 app
python3 wireless/tools/burn.py board_a wireless/ai-bs21-32s-kit/build-probe/bs21_all_in_one.fwpkg

# 抓 log
python3 wireless/tools/capture_uart.py --board-a --duration 60 --odir /tmp --ts
```

## 双模块调试（M8 逆向阶段）

固定角色：
- **board_a = 接收器侧**：烧 `sle_probe`（client，扫描/连接/发现/读写实验）
- **board_b = 手柄侧**：烧 `flydigi_decoy`（server，镜像手柄属性表，记录 dongle 行为）

双 build 目录（协议栈库随 sle_role 不同，不能共用）：

```bash
cmake -S wireless/ai-bs21-32s-kit -B wireless/ai-bs21-32s-kit/build-decoy -DBS21_APP=flydigi_decoy
cmake -S wireless/ai-bs21-32s-kit -B wireless/ai-bs21-32s-kit/build-probe -DBS21_APP=sle_probe
python3 wireless/tools/burn.py board_b wireless/ai-bs21-32s-kit/build-decoy/bs21_all_in_one.fwpkg
python3 wireless/tools/burn.py board_a wireless/ai-bs21-32s-kit/build-probe/bs21_all_in_one.fwpkg
```

## 已知 SDK 陷阱（probe 侧）

- `ssapc_find_structure_cb` / `ssapc_find_property_cbk` 返回的 UUID 是错的：
  总是 37BE（=0xBE33，描述符 UUID），不是真实的 UUID。SDK 解析器读错了偏移。
  绕过方法：在 `probe_dump_discovery_cfm` 里从原始 PDU 解析 UUID，查表替换。
- `ssapc_read_req` 签名是 `(client_id, conn_id, handle, type)`，不是结构体指针。
- SDK 拒绝 find type 2/4/5（REFERENCE_SERVICE/METHOD/EVENT）err=0x7，即使 UUID
  正确也不支持。核心发现（type 0/1/3）+ 读取属性值不受影响。
- 防掩耳铁律：probe 的 RX 原始 PDU（`RX len=N:` 行）= 数据真相；SDK 回调里的
  UUID/start_hdl 等 = 观察者侧值，可能不等于真相。双侧 diff 以原始 PDU 为准。

## 开发环境搭建

详见 `docs/bs21-development.md`
