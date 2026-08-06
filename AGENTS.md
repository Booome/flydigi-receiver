# Flydigi Receiver Project

## 编译与烧录

编译和烧录方法参见 `docs/build-and-flash.md`。

关键要点：
- 使用 `make build-*` / `make flash-*` 命令，不要直接调用 west
- NCS 环境通过 `nrfutil sdk-manager toolchain launch` 隔离注入，不要全局设置 LD_LIBRARY_PATH
- 编译时需设置 `ZEPHYR_BASE=~/ncs/v3.4.0/zephyr`（Makefile 已处理），因为本项目不在 NCS workspace 内
- nRF52840 Dongle 命令行烧录需要先生成 DFU zip 包（`nrfutil nrf5sdk-tools pkg generate`），不能直接烧录 .hex/.elf（Programmer app GUI 可以直接烧 ELF）
- Dongle 进入 DFU 模式：按侧面 Reset 键
