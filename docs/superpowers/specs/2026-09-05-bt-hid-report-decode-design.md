# BT HID 报告解码设计（`apps/default/` 迭代）

## 一、背景与目标

前序：
- **bt-hid-host-capture**（M11）已完成——`apps/default/` 用三层候选算法连上飞智八爪鱼5 的 BR/EDR HID 设备，稳定采集 **15 字节** 原始 HID 输入报告（~36/s），以 hex 打串口。

本里程碑：把**原始 hex 变成有意义的输入状态**。三步：
1. **Descriptor 确认**：连接后从手柄拉取并解析 HID Report Descriptor，机器化确认输入报告的 report id / type / 长度 / usage，并把 descriptor 原始字节 dump 出来供离线逐项分析 → 得到"字节/位 → 语义字段"映射表，存档进 `docs/`。
2. **struct 解码**：按确认后的布局定义结构体，把每份报告解成具名字段（按钮位、双摇杆四轴、双扳机、d-pad、menu/guide 键）。
3. **实测验证**：逐个物理操作（每个键、摇杆推满、扳机到底）核对解码结果与映射表一致；不一致以实测为准并回写文档。

**关键立场（防掩耳盗铃）**：捕获的空闲帧中心值为 `00 7f ff 80 00 80 ff 80 …`（0x7f/0x80/0xff 混排、非直觉），**不得**直接照搬"标准 Xbox 360 布局"硬套字段；布局必须由 descriptor + 实测共同确立。esp_hid 自带的 `esp_hid_parse_report_map()` 只给出"报告级"元数据（id/type/长度/usage），**不给**逐位语义，故逐位映射仍靠 descriptor 分析 + 实测。

**非目标**：转发到 PC、HID Output / 震动、SLE、全量 hex 常显、多设备并发。

## 二、技术栈与约束

| 项 | 选型 |
|---|---|
| 平台 | ESP32-WROOM-32E（board_a），`BOARD_A_TYPE=esp32-wroom-32e`（DTR 复位），沿用 |
| 框架 | ESP-IDF v6.0.2（`/opt/esp-idf` 只读），BluedR，esp_hid 组件 |
| descriptor API | `esp_hidh_dev_report_maps_get(dev, &num_maps, &maps)` → `esp_hid_raw_report_map_t{const uint8_t *data; uint16_t len;}`；`esp_hid_parse_report_map(data,len)` → `esp_hid_report_map_t{usage,appearance,reports_len,reports[]}`，`reports[i]` = `esp_hid_report_item_t{map_index,report_id,report_type,protocol_mode,usage,value_len}`；用完 `esp_hid_free_report_map()` |
| 归属 | **`apps/default/`**（主 app，不新建 app）。解码逻辑独立成 `main/hid_report.{c,h}`，`main.c` 只接事件 |
| 输出 | 变化才打印（见 §五）；descriptor 连接时一次性 dump |
| 构建/烧录/抓 log | `tools/build.py`（默认 `default`）、`tools/burn.py`、`tools/capture_uart.py --board-a --rst-a` |

### 硬约束
- 不动 `apps/hello_world/` / `apps/bt_scan/`；候选算法（三层）逻辑不动，只新增解码/打印。
- 非侵入式：不改 ESP-IDF；`esp_hid` API 只调用不改。
- 不假设 Xbox 布局：字段偏移以 descriptor + 实测为准。
- 每个 `.c/.h` 改完立即 `clang-format -i`；`CMakeLists.txt` 改完 `cmake-format -i`（AGENTS.md 铁律）。

## 三、项目布局

```
bluetooth/esp32-wroom-32e/
├── apps/default/
│   └── main/
│       ├── main.c            # 改：OPEN 事件 dump descriptor；INPUT 事件 解码 + 变化打印
│       ├── hid_report.c       # 新增：descriptor dump/解析 + 解码 + 状态打印
│       ├── hid_report.h       # 新增：xinput 结构 + 函数声明 + 字段名
│       └── CMakeLists.txt     # 改：SRCS 加 "hid_report.c"
├── docs/
│   ├── apex5-hid-descriptor.md  # 新增：descriptor 原始 hex + 逐项分析 + 报告元数据（parse 结果）
│   └── apex5-hid-input-map.md    # 新增：实测 button/axis → 字节/位 映射表 + 摇杆/扳机量程符号
└── ...
```

## 四、`hid_report.{c,h}` 设计

分节顺序遵循 AGENTS.md（include→define→type→global→函数）。对外：

```c
// hid_report.h
typedef struct {
    // 字段/偏移在 descriptor 分析 + 实测后固化；下面是占位说明，最终顺序/宽度以 docs/apex5-hid-input-map.md 为准
    uint32_t buttons;      // 按钮位域（A/B/X/Y/LB/RB/Back/Start/Guide/L3/R3）
    uint8_t  dpad;         // 0..8 方向（或并入 buttons）
    uint8_t  left_trigger; // 0x00..0xFF 或 0x0000..0xFFFF（以实测量程为准）
    uint8_t  right_trigger;
    int16_t  lx, ly, rx, ry; // 摇杆轴（符号/字节序以实测为准）
} apex5_xinput_t;

void hid_dump_report_map(esp_hidh_dev_t *dev);   // OPEN 事件调用：print raw map hex + parse 结果
bool hid_decode(const uint8_t *buf, uint16_t len, apex5_xinput_t *out); // 返回是否有效报告
void hid_print_state(const apex5_xinput_t *prev, const apex5_xinput_t *cur); // 变化才 print
```

`main.c` 侧：
- `ESP_HIDH_OPEN_EVENT`（status OK）→ `hid_dump_report_map(dev)`（一次性）
- `ESP_HIDH_INPUT_EVENT` → `if (hid_decode(param->input.data, param->input.length, &cur))` 与上次 `memcmp` 不等则 `hid_print_state(&prev,&cur); prev=cur;`
- 全局 `static apex5_xinput_t g_last_state;` 记录上一次

`hid_dump_report_map` 内部：
```c
esp_hidh_dev_report_maps_get(dev,&n,&maps);
for i in maps: printf descriptor hex (分块, 每 16 字节一行)
esp_hid_report_map_t *parsed = esp_hid_parse_report_map(maps[0].data, maps[0].len);
for r in parsed->reports: printf "[hid] map: idx/id/type/usage/len"
esp_hid_free_report_map(parsed);
```

> 15 字节报告里，`value_len` 应解析出 ~15（INPUT）——用它交叉确认长度；逐位语义不来自 parse，来自下面的分析。

## 五、输出格式

连接时一次性：
```
[hid] descriptor[0] len=<N> bytes:
[hid]  05 01 09 05 a1 01 05 09 19 01 29 0a 15 00 25 01 ...
[hid] report: map=0 id=0 type=INPUT usage=GAMEPAD len=15
[hid] open: addr=b5:5d:e7:98:54:75 transport=BR_EDR
```
运行期（仅状态变化时）：
```
[hid] state: btn=A|LB dpad=none lt=0 rt=0 lx=0 ly=0 rx=0 ry=0
[hid] state: btn=-    dpad=U   lt=0 rt=0 lx=0 ly=0 rx=0 ry=0
```
- `btn=` 用 `|` 连接当前按下的键名，无键 = `-`
- `dpad=` none/U/D/L/R（组合如 UL）
- 轴/扳机为十进制有符号/无符号（依实测确定）
- 空闲第一帧也打印一次作为基线

## 六、验证协议（逐键实测 → 产出映射表）

前置：board_a 已烧 `default`，`.env` 有 `BOARD_A_TYPE=esp32-wroom-32e`；手柄 BT 模式 + 配对（快闪），并提醒用户手柄空闲 10–30s 省电（见 AGENTS.md）。

1. **dump descriptor**：连接后抓 `[hid] descriptor` + `[hid] report` 行，存进 `docs/apex5-hid-descriptor.md`；对照 HID 规范（Usage Page 0x01 Generic Desktop、按钮/轴 item、Report Size/Count/ID）逐项标注字节偏移与位。
2. **逐键**：A/B/X/Y、LB/RB、LT/RT（缓压到底）、Back/Select、Start、Guide、L3/R3、D-pad 上/下/左/右 —— 每按一下记 `[hid] state` 里翻转的字段，写入 `apex5-hid-input-map.md`（bit↔键名）。
3. **摇杆**：左/右各推满 X+/X-/Y+/Y- 与回中，记两字节的**字节序**（LE/BE）、**符号**（0x8000 中心? 还是 0x0000?）、**量程**。
4. **扳机**：LT/RT 从松到底，记量程（0–255 还是 0–1023 等）与所在字节。
5. **对账**：映射表 vs descriptor 解析；冲突以实测为准，回写文档并注明。

## 七、产物 / 文档同步

| 文件 | 内容 |
|---|---|
| `docs/apex5-hid-descriptor.md` | 原始 descriptor hex + 逐项分析 + `esp_hid_parse` 报告元数据 |
| `docs/apex5-hid-input-map.md` | 实测键/轴→字节/位映射 + 量程/符号/字节序结论 |
| `apps/default/main/hid_report.{c,h}` | 解码实现 |
| `apps/default/main/main.c` | 接入 dump + 解码 + 变化打印 |
| `apps/default/README.md` | 说明解码与输出格式 |
| `AGENTS.md` | 该里程碑标完成 + 一句解码结论（报告=Xbox 360 风格?/飞智扩展）|

## 八、范围外（不做）

- ❌ 转发 PC / 虚拟手柄（USB/Wi-Fi）
- ❌ HID Output（震动）——后续可能单列里程碑
- ❌ 全量 hex 常显（改状态驱动）
- ❌ SLE、NimBLE、其他 BT 设备类型
- ❌ 新建 app（解码进 `apps/default/`）

## 九、验证矩阵

| # | 步骤 | 通过标准 |
|---|---|---|
| 1 | `build.py`（默认 default）| 编译通过 |
| 2 | `burn.py` | 烧 board_a 成功（DTR）|
| 3 | capture：连接 | 见 `[hid] descriptor` + `[hid] report ... len=15` + `[hid] open` |
| 4 | 空闲基线 | 一条 `[hid] state` 全默认，随后无刷屏 |
| 5 | 按 A | `btn=` 出现 `A`，松开消失（对应位与 descriptor/实测表一致）|
| 6 | 逐键 | 每个物理键都有可区分字段变化 |
| 7 | 摇杆 | 推满对应轴单调变化、回中归零；符号/字节序记录 |
| 8 | 扳机 | LT/RT 数值随按压变化到量程端点 |
| 9 | 一致性 | `apex5-hid-input-map.md` 与实机输出、descriptor 三方一致 |

## 十、风险与决策

- **descriptor 可能较长**（数百字节）→ 分块 hex（每行16字节）dump；解析只用 esp_hid parse 拿报告级元数据，逐位靠分析+实测。
- **飞智可能改/扩映射**（非纯 Xbox 360）→ 不预设，实测为准；文档记录差异。
- **摇杆/扳机字节序与符号**未知 → 验证步骤 3/4 专门测；`apex5_xinput_t` 字段宽度据此定。
- **报告可能多路**（parse 出多个 INPUT report id）→ 若手柄发多类报告，先只解 `value_len≈15` 的主输入报告，其余记录到文档备用。
- **UI 刷屏**→ 已用"变化才打印"规避。
- **formatting**：新 `.c/.h` 与改动的 `CMakeLists.txt` 提交前 `clang-format`/`cmake-format`。

## 十一、完成定义

- `apps/default/` 连接手柄后自动 dump 并解析 descriptor；输入报告解成具名字段并**仅变化时**打印。
- 逐键/摇杆/扳机实测通过，映射表 + descriptor 分析两份 docs 落库且与实机一致。
- `main.c`+`hid_report.c/.h`+`docs/*`+`AGENTS.md`+`README` 更新；全部格式化；build + 实机验证通过。
- 提交到 `bt-hid-report-decode` 分支，未合并（等用户指令）。