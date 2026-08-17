# BS21 ROM Patch 机制分析

> 主题：Hi2821 (BS21) 芯片的 ROM patch（表驱动补丁）机制，包括配置、生成、加载、
> 初始化原理，以及本项目在构建/烧录链路上遇到的"patch 表缺失"问题与修复。
> 适用构建：`t_broadcaster`（`-B wireless/bs21/build/t_broadcaster -DBS21_APP=t_broadcaster`）。

## 一、为什么需要 ROM patch

BS21 的蓝牙协议栈大量代码固化在 **ROM**（`0x15400`~`0x40000`），ROM 与 SDK 库
（`libbgtp.a` 等）均为**闭源**。当 ROM 里存在 bug（例如下文
`evt_prog_finish_eeq_isr` 直接 `LOS_MemFree` 静态池元素），无法直接改 ROM，只能通过
**patch 表**把被 patch 的 ROM 函数入口重定向到 RAM/ITCM 中的补丁函数。

一个已知的 ROM bug 实例：

- `evt_prog_finish_eeq_isr`（ROM `0x1aa3e`）释放事件调度器元素时**绕过**
  `es_free_eeq_elt()` 直接调 `LOS_MemFree()`，而元素来自静态 BSS 池
  `g_es_eeq_elt_pool`（6×32B），地址超出 heap pool 范围，运行时打印：
  `fail to free memory, pool=[0x20002660], mem=[...]`。
- SDK 自带补丁 `evt_prog_finish_eeq_isr_patch`（用 `es_free_eeq_elt`），
  只要 patch 表生效该 bug 即被修复。

## 二、Patch 表组成

一个 patch 表由三部分构成（生成脚本为 SDK
`build/script/patch/patch_riscv.py`）：

| 文件 | 内容 | 说明 |
|------|------|------|
| `cmp.bin` | CMP（比较表） | 列出全部被 patch 的 **ROM 函数地址**，供硬件比较取指地址 |
| `tbl.bin` | TBL（跳转表） | 每个被 patch 函数对应一条跳转指令（JAL / AUIPC+JALR） |
| `patch.bin` | RW（补丁代码） | 补丁函数的实际代码（链接到 ITCM/RAM） |

三者最终被打包进 application 的 `.patch_data` 段（见第四节）。

## 三、Patch 配置：acore.cfg

配置路径：`${SDK_ROOT}/build/config/target_config/bs21/patch_config/acore.cfg`
（本项目实际生效的是该通用配置，`bs21-n1100-rcu.cfg` 不存在）。

关键项：

```ini
[Patch Info]
Patch_Cpu_Core = APPLICATION
Patch_TBL_Address     = 0x00000300   ; TBL 在 bin 中的偏移
Patch_TBL_Run_Address = 0x00040000   ; TBL 运行地址（ITCM）
Table_Max_Size  = 4
Table_Reg_Size  = 4
TABLE_REG_CONUT = 128                ; 最多 128 个比较项

[ROM Info]
ROM_Address = 0x00015400
ROM_Size    = 0x0002ac00
```

`[Function]` 段列出 `ROM函数 补丁函数` 对，共 **36** 个：

- **btc patch（23 个）**：`evt_task_ble_acl_*`、`lm_ble_adv_*`、`dts_malloc`、
  `dts_hci_malloc`、`evt_prog_finish_eeq_isr`、`dpc_*`、`es_process_cancel_cbk` 等。
- **bth patch（13 个）**：`l2cap_*`、`att_*`、`gatt_*`、`smp_*`、`bt_init_stack` 等。

### 生成的表布局（bin 内）

patch 脚本把三表写入 application.bin 的固定偏移（受 `DATA_PATCH_COUNT=2` 影响）：

| 偏移 | 内容 |
|------|------|
| `0x300` | TBL：36 条跳转指令（每条 4B），不足 128 条补零 |
| `0x500` | CMP：头 `[0, 0x40000, 0x24]` + 36 个 ROM 函数地址（bit0 置 1 作标记） |

### TBL 跳转指令的语义（关键）

- **short jump**：当 `patch_addr - rom_addr` 在 JAL 的 ±1MB 范围内时用 JAL，
  `imm = patch_addr - rom_addr`。
- **long jump**：超范围时用 `AUIPC + JALR`（基址寄存器 x6）。

JAL 的 **pc 基准是 ROM 函数的逻辑地址**（不是 TBL 的物理地址）：
CPU 取指 ROM 函数 `func_addr` 时，硬件把取指重映射到 ITCM 中的 TBL 条目，
但 CPU 看到的 pc 仍是 `func_addr`，因此 JAL 跳转目标 = `func_addr + imm =
patch_addr`，正好落到 ITCM 补丁函数。

> 校验技巧：从 bin `0x308` 起读 36 条 JAL 解码出的"目标"是
> `0x40008 + imm`（用 TBL 物理地址当 pc），数值并不等于 patch 函数地址；
> 必须从 CMP 表取 `rom_addr`，用 `rom_addr + imm` 才是真正的 patch 函数地址。

## 四、Patch 数据的链接与加载

application.elf 中：

- **`.patch_data` 段**：VMA=`0x40000`（ITCM，运行地址），
  LMA=`0x9010b600`（flash，存储地址），bin 偏移 `0x300`，大小 `0x410` 字节。
- startup 汇编 `set_patch_data_loop` 把 `0x410` 字节从 flash 拷到 ITCM
  `0x40000`（TBL 实际落在 `0x40008`，偏移来自 `DATA_PATCH_COUNT=2` 的偏移计算）。

### Patch 初始化

`func_patch_init`（app，`0x45fd0`）配置 patch 硬件：

- `cmp_start_addr = 0x40200`（CMP 表所在 ITCM 地址）
- `remap_addr     = 0x40000`（TBL 运行地址）
- `off_region     = false`

`patch_init`（app，`0x45f80`）把 128 个 word 从 RAM `0x4020C` 写入硬件寄存器
`0xe0000+16..`，并把 remap 地址写入 `0xe0000+4`，从而启动重映射。

调用时机：

- **SDK 原生**：`pm_port_cpu_resume`（`0x42fda` 附近，调用点 `0x4318a`），
  即**每次 CPU 从低功耗唤醒**都重新配置 patch。
- **本项目**：另在 `apps/t_broadcaster/main.c` 的 `axk_main()` 入口显式调用
  `func_patch_init()`。

## 五、如何验证 patch 表已进入固件

### 静态检查（烧录前）

用本仓库工具检查 fwpkg 的 application 分区是否含 TBL/CMP：

```bash
python3 - <<'EOF'
d = open('wireless/bs21/build/t_broadcaster/bs21_all_in_one.fwpkg','rb').read()
print("TBL:", bytes.fromhex('efa28239') in d)   # 第一条 JAL
print("CMP:", bytes.fromhex('000000000000040024000000') in d)
EOF
```

更强校验：从 application.bin 的 `0x308`（TBL）与 `0x500`（CMP）读取并交叉验证
`rom_addr + imm == patch 函数地址`（见第三节的校验技巧），并可与 nm 符号比对，
例如 `0x1aa3e + 0x2b6e4 == 0x46122`（`evt_prog_finish_eeq_isr_patch`）。

### 动态检查（烧录后）

- 观察串口：SLE 广播/扫描正常启动，且**不再出现** `fail to free memory` 与
  `<addr> out of range!`。
- 严格验证需触发事件调度（连接、扫描等），让 `evt_prog_finish_eeq_isr` 真正运行。

## 六、历史问题：fwpkg 里 patch 表缺失

### 现象

烧录后的板子一直出现 `fail to free memory`，排查发现**板子上跑的 fwpkg 的
application 分区根本没有 patch 表**：bin `0x308` 是启动代码（`6f004000...`）而非
JAL，`0x500` 全零，整个 bin 搜不到 `patch_init` 的 `lui a4,0xe0000`（`37 07 00 e0`）。

### 根因链

1. **构建时序竞争（CMakeLists）**：`GENERAT_ROM_PATCH`（嵌入 patch 表）与
   `GENERAT_SIGNBIN`（复制 bin → SDK 目录 → 签名）都依赖 `GENERAT_BIN` 并行执行；
   `GENERAT_SIGNBIN` 仅 `DEPENDS GENERAT_BIN`，可能在 patch 表嵌入前就复制了无表
   的 bin。
2. **打包路径**：`packet.py`（`${SDK_ROOT}/tools/pkg/chip_packet/bs2x/packet.py`）
   打包的是 **SDK output 目录**的 `application_sign.bin`
   （`${SDK_ROOT}/output/bs21/acore/bs21-n1100-rcu/`）。一旦该文件是被竞态复制
   的无表 bin，fwpkg 即无表。
3. **烧录路径**：`burn.py` 默认烧 `wireless/bs21/build/t_broadcaster/bs21_all_in_one.fwpkg`
   （构建的 package 目标会把 SDK output 的 fwpkg 复制到 `PROJECT_BINARY_DIR`）。

结果：整条链路最后得到的是**无 patch 表**的固件，`func_patch_init` 配置了硬件
但 ITCM 里根本没有表，patch 自然不生效。

### 修复

- `wireless/bs21/CMakeLists.txt`：`GENERAT_SIGNBIN` 的依赖改为
  `GENERAT_BIN GENERAT_ROM_PATCH`（`if(TARGET GENERAT_ROM_PATCH)` 保护），
  保证签名/打包一定发生在 patch 表嵌入之后。
- 重新构建后，SDK output 的 `application.bin`、`application_sign.bin` 与
  生成的 fwpkg 均含 patch 表（TBL+CMP 验证通过）。

## 七、Workaround 历史：`__wrap_LOS_MemFree`

在 patch 未生效期间，为消除 `fail to free memory` 加过链接器级 workaround：

- `scripts/gen-config.py` 追加 `-Wl,--wrap=LOS_MemFree`；
- `sdk-compat/los_memfree_wrap.c` 实现 `__wrap_LOS_MemFree`：地址落在
  `[g_es_eeq_elt_pool, +0xC0)` 时转 `es_free_eeq_elt()`，否则走真实
  `__real_LOS_MemFree`。

**注意**：该 wrap 有误判风险（若 heap 元素地址落入静态池范围会被错误回收）。

### 移除与因果验证

patch 表修复后，wrap 已多余。验证方法（**去掉 wrap 仍无 fail to free =
基本证明 patch 生效**）：

1. `sdk-compat/CMakeLists.txt` 去掉 `los_memfree_wrap.c`；
2. `scripts/gen-config.py` 去掉 `--wrap=LOS_MemFree`（否则链接期大量
   `undefined reference to __wrap_LOS_MemFree`）；
3. 重建、烧录、抓串口：`fail to free` / `out of range` 均 0 次，SLE 广播正常。

## 八、常用命令与关键符号

### 工具

```bash
# nm（工具链随 SDK 附带）
NM=$HOME/.local/Ai-BS21_SDK/tools/bin/compiler/riscv/cc_riscv32_musl_b010/cc_riscv32_musl_fp/bin/riscv32-linux-musl-nm
$NM wireless/bs21/build/t_broadcaster/application.elf | grep -iE "patch|evt_prog_finish|dts_free|LOS_MemFree"

# 烧录（-a 指定 app）
python3 wireless/bs21/tools/burn.py board_a -a t_broadcaster
```

### 关键地址（`t_broadcaster` 构建）

| 符号 | 类型 | 地址 |
|------|------|------|
| `patch_init` | app (ITCM) | `0x45f80` |
| `func_patch_init` | app (ITCM) | `0x45fd0` |
| `evt_task_ble_acl_refresh_next_peripheral_time_patch` | app (ITCM) | `0x45fec` |
| `evt_prog_finish_eeq_isr` | ROM | `0x1aa3e` |
| `evt_prog_finish_eeq_isr_patch` | app (ITCM) | `0x46122` |
| `dts_malloc_patch` | app (ITCM) | `0x462ea` |
| `es_free_eeq_elt` | app (ITCM) | `0x47912` |
| `dts_free` | ROM | `0x181ae` |
| `LOS_MemFree` | app | `0x45568` |

> 补丁函数通常链接在 ITCM `0x45xxx`~`0x46xxx`（具体地址随构建变化，以 nm 为准）。

## 九、相关文件索引

| 文件 | 作用 |
|------|------|
| `${SDK_ROOT}/build/config/target_config/bs21/patch_config/acore.cfg` | patch 函数清单 |
| `${SDK_ROOT}/build/script/patch/patch_riscv.py` | 生成 TBL/CMP/RW 并写入 bin |
| `${SDK_ROOT}/build/cmake/build_elf_info.cmake` | `GENERAT_ROM_PATCH` 等 target |
| `wireless/bs21/CMakeLists.txt` | `GENERAT_SIGNBIN` 依赖修复 |
| `wireless/bs21/scripts/gen-config.py` | 注入/移除 `--wrap=LOS_MemFree` |
| `wireless/bs21/sdk-compat/los_memfree_wrap.c` | 已停用的 wrap（可删除） |
| `wireless/bs21/tools/burn.py` | 烧录（`-a/--app` 指定 app） |
