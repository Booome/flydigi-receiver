# 双 CDC ACM 设计：Debug + Data 串口分离

## 一、概述

将单个 CDC ACM 实例拆分为两个：Debug 口（printk/LOG）和 Data 口
（formatter 功能数据），通过 USB 接口描述符字符串区分。

## 二、现状

- `cdc_acm_serial.dtsi`（板级 DTSI 自动 include）定义了 1 个 CDC ACM
  实例 `board_cdc_acm_uart`，已设为 `zephyr,console`
- 该实例无 `label` 属性，USB 接口字符串为空（iInterface=0）
- `output_cdc.c` 用 `DEVICE_DT_GET_ONE` 获取该实例
- `CONFIG_UART_CONSOLE` 未启用，printk/LOG 实际不输出

## 三、方案

### 3.1 设备树 overlay（新建 `app.overlay`）

```dts
&board_cdc_acm_uart {
    label = "Flydigi-Debug";
};

&zephyr_udc0 {
    cdc_acm_data: cdc_acm_data {
        compatible = "zephyr,cdc-acm-uart";
        label = "Flydigi-Data";
    };
};
```

- 给现有 `board_cdc_acm_uart` 加接口字符串，作为 Debug 口
- 新增 `cdc_acm_data` 节点作为 Data 口
- 不修改 `chosen`（已正确指向 console）

### 3.2 output_cdc.c 修改

```c
static const struct device *const cdc_dev =
    DEVICE_DT_GET(DT_NODELABEL(cdc_acm_data));
```

### 3.3 prj.conf 修改

```ini
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_LOG_BACKEND_UART=y
```

`CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=n` 保持不变。

## 四、数据流

```
printk/LOG -> console -> board_cdc_acm_uart (Flydigi-Debug)
formatter  -> output_send() -> cdc_acm_data (Flydigi-Data)
```

## 五、关键设计点

- **不删除** `board_cdc_acm_uart`，不修改 `chosen`
- DTR 检测：Data 口保持现有逻辑；Debug 口由 console 驱动管理
- `uart_poll_out` 在 USB 未就绪时缓冲数据，USB 初始化后发送
- nRF52840 有 8 个双向端点，2 个 CDC ACM 实例（各占 2 IN + 1 OUT）够用

## 六、验证标准

- 编译成功
- Linux 端看到 2 个 `/dev/ttyACM*`
- sysfs 接口字符串：`Flydigi-Debug` / `Flydigi-Data`
- Debug 口收到 printk/LOG 输出
- Data 口收到 formatter 数据
- 单元测试回归通过
