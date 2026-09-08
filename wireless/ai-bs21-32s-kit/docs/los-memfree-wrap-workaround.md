# LOS_MemFree wrap 垫片 — ROM bug 分析

> 对应代码：`sdk-compat/los_memfree_wrap.c`

## 根因

ROM 函数 `evt_prog_finish_eeq_isr()`（0x1aa3e）释放事件调度器元素时**直接调用
`LOS_MemFree()`**，绕过了 `es_free_eeq_elt()`。该元素来自
`es_allocate_eeq_elt()` 的静态 BSS 池（`g_es_eeq_elt_pool`，6×32 字节），
`LOS_MemFree` 判定地址越界并打印：

```
"<addr> out of range!"
"fail to free memory, pool=[0x20002660], mem=[<addr>]"
```

SDK 里存在正确的 RAM patch（`evt_prog_finish_eeq_isr_patch` 使用
`es_free_eeq_elt`），但本 build 的 patch 机制**没有**给
`evt_prog_finish_eeq_isr` 装 remap，跑的仍是 ROM 版本。

## 修复方式

ROM 与 SDK 库闭源、无法改调用点，改为**包装 `LOS_MemFree`**：静态池地址重定向到
`es_free_eeq_elt()`（归还池内），堆元素仍走真实 `LOS_MemFree`。该 shim 是所有错误
释放的唯一漏斗，覆盖面全、风险最小。

## SDK 升级后是否可删（判据）

1. 反汇编 `libbgtp.a` / ROM，检查 `evt_prog_finish_eeq_isr+0x34`（0x1aa72）的
   `LOS_MemFree` 调用是否已被 remap 到 RAM patch；
2. 运行时判据：reset 后不再出现 "fail to free memory" 行（即使没有本 wrap）。

满足任一即可删除本文件。
