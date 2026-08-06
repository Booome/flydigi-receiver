# 飞智八爪鱼5 自制接收器项目

## 项目目标

基于 nRF52840 Dongle，逆向工程飞智八爪鱼5的无线协议，实现一个自制的USB接收器。

## 硬件环境

| 设备 | 型号 | 用途 |
|------|------|------|
| 手柄 | 飞智八爪鱼5 (Flydigi Apex 5) | 被逆向的目标设备 |
| 开发板 | nRF52840 Dongle | 嗅探器 + 接收器实现 |
| 电脑 | Arch Linux (KDE Plasma Wayland) | 开发和调试 |

## 飞智八爪鱼5 连接模式

八爪鱼5支持三种连接方式：

### 1. USB 有线模式
- Type-C 直连
- 标准 XInput/DirectInput 协议
- 最简单，但不是我们要逆向的目标

### 2. 蓝牙 (BLE) 模式
- BLE HID over GATT 协议
- 手柄作为标准 BLE HID 设备
- 支持 XInput/DirectInput 模式切换
- 手机/平板/PC 蓝牙连接

### 3. 2.4GHz 无线模式
- 飞智专用 USB 接收器
- **私有 XInput 协议**（非标准）
- 需要专用驱动才能被系统识别
- 这是最有挑战性的逆向目标

## 已有的开源项目

### 项目 1：flydigi-vader5（Linux 驱动）
- **地址**: https://github.com/BANANASJIM/flydigi-vader5
- **说明**: 飞智黑武士5 Pro 的 Linux 用户态驱动
- **功能**: 支持 2.4G USB 接收器，Xbox Elite 模拟、陀螺仪、按键重映射
- **价值**: 包含完整的协议逆向文档

### 项目 2：Flydigi5Pico（USB 协议桥接）
- **地址**: https://github.com/ruomox/Flydigi5Pico
- **说明**: 基于 RP2350 的硬件协议桥接
- **功能**: 将飞智私有 XInput 协议转换为标准 Xbox 接口
- **价值**: 包含完整的协议分析和 USB 描述符

### 项目 3：vader3（蓝牙驱动）
- **地址**: https://github.com/ahungry/vader3
- **说明**: 飞智黑武士3 的 Linux 蓝牙驱动
- **功能**: 实现了 HID over GATT 协议
- **价值**: 支持额外的背键、C/Z 键等

### 项目 4：flydigictl（配置工具）
- **地址**: https://github.com/pipe01/flydigictl
- **说明**: 通用的飞智手柄配置工具
- **功能**: 通过 D-Bus 接口管理手柄配置

## 关键协议信息

### 飞智 USB Vendor ID
```
VID: D7D7 (飞智的 USB Vendor ID)
PID: 0041 (黑武士3，八爪鱼5可能不同)
```

### 蓝牙协议
- 协议: BLUETOOTH HID v1.01
- 设备名: Flydigi VADER3P (以黑武士3为例)
- 支持模式: XInput / DirectInput 切换

### 2.4GHz 私有协议
- 使用飞智专用 USB 接收器
- 私有 XInput 协议（非标准）
- 需要专用驱动才能被系统识别
- macOS 不支持，需要协议桥接

## 开发计划

### 阶段 1：蓝牙协议分析（预计 1-2 天）
**目标**: 理解八爪鱼5的蓝牙 HID 协议

**步骤**:
1. 安装 nRF Connect for Desktop
2. 打开 nRF Sniffer for BLE 工具
3. 刷 nRF52840 Dongle 固件（一键操作）
4. 打开 Wireshark
5. 让八爪鱼进入蓝牙配对模式
6. 抓包分析 HID 报告格式

**预期成果**:
- 完整的蓝牙数据包捕获
- HID 报告格式文档
- 飞智自定义服务分析

### 阶段 2：2.4GHz 协议分析（预计 1-2 周）
**目标**: 理解飞智 2.4G 私有协议

**步骤**:
1. 参考 flydigi-vader5 项目的协议文档
2. 用 nRF52840 嗅探私有协议
3. 分析数据帧结构
4. 理解握手机制

**预期成果**:
- 2.4G 协议完整文档
- 数据帧格式定义
- 加密/认证机制分析

### 阶段 3：接收器实现（预计 2-4 周）
**目标**: 实现自制 USB 接收器

**步骤**:
1. 用 nRF52840 写接收器固件
2. 实现 BLE HID 服务
3. 实现 2.4GHz 私有协议
4. 测试兼容性

**预期成果**:
- 可工作的接收器固件
- 支持八爪鱼5的全部功能
- 兼容主流操作系统

## 技术栈

### 固件开发
- **框架**: Zephyr RTOS (nRF Connect SDK)
- **语言**: C/C++
- **工具**: nRF Connect for Desktop

### 协议分析
- **嗅探器**: nRF Sniffer for BLE
- **抓包工具**: Wireshark
- **脚本**: Python (数据解析)

### 接收器实现
- **BLE 服务**: HID over GATT
- **USB 接口**: XInput/DirectInput
- **驱动**: Linux HID 驱动

## 当前进度

### 已完成
- [x] 项目目录创建
- [x] 初步协议调研
- [x] 找到相关开源项目

### 进行中
- [ ] 详细协议分析
- [ ] 开发环境搭建

### 待完成
- [ ] 蓝牙协议嗅探
- [ ] 2.4G 协议分析
- [ ] 接收器固件开发
- [ ] 测试和调试

## 参考资料

1. **nRF52840 产品规格**: https://www.nordicsemi.com/Products/nRF52840
2. **Zephyr RTOS 文档**: https://docs.zephyrproject.org/
3. **BLE HID 规范**: Bluetooth HID over GATT Specification
4. **XInput 协议**: Microsoft XInput API 文档
5. **飞智官方 SDK**: https://www.flydigi.com/sdk (已下线)

## 注意事项

1. **法律风险**: 逆向工程可能涉及知识产权问题，仅用于学习研究
2. **硬件损坏**: 嗅探过程中可能损坏手柄或接收器
3. **兼容性**: 不同批次的手柄可能使用不同芯片
4. **加密协议**: 2.4G 协议可能包含加密，需要额外分析

## 下一步行动

请阅读本文档后，告诉我：
1. 你想从哪个阶段开始？
2. 是否需要我帮你搭建开发环境？
3. 是否有其他问题需要解答？
