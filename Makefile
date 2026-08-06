# nRF Connect SDK (NCS) Build System
# All NCS environment vars are injected at build time by
# `nrfutil sdk-manager toolchain launch`, keeping the global
# shell environment clean.

NCS_VERSION := v3.4.0
BOARD := nrf52840dongle
LAUNCH := nrfutil sdk-manager toolchain launch --ncs-version $(NCS_VERSION) --
NCS_HOME := $(HOME)/ncs/$(NCS_VERSION)
ZEPHYR_BASE := $(NCS_HOME)/zephyr
DFU_TRAITS := --traits nordicDfu

# ── Sample apps (in NCS tree, for verification) ──────────────
BLINKY_SRC := $(NCS_HOME)/zephyr/samples/basic/blinky
CDC_ACM_SRC := $(NCS_HOME)/zephyr/samples/subsys/usb/cdc_acm

.PHONY: build-blinky build-cdc flash-blinky flash-cdc
.PHONY: build-fw flash-fw build-test run-test
.PHONY: clean clean-blinky clean-cdc clean-fw clean-test devices

# ── Build ────────────────────────────────────────────────────
# This project lives outside the NCS folder hierarchy, so west
# cannot auto-discover the workspace. Setting ZEPHYR_BASE lets
# west locate the Zephyr tree and load extension commands (like
# `build`). See:
#   sdk-nrf/doc/nrf/app_dev/create_application.rst (ZEPHYR_BASE)
build-blinky:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b $(BOARD) $(BLINKY_SRC) -d build-blinky --no-sysbuild

build-cdc:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b $(BOARD) $(CDC_ACM_SRC) -d build-cdc --no-sysbuild

# ── Flash (requires Dongle in DFU mode) ──────────────────────
# nRF52840 Dongle uses Nordic secure DFU, which requires a SdfuZip
# package (not a raw .hex). The zip is generated from .hex by
# `nrfutil nrf5sdk-tools pkg generate`, then flashed with
# `nrfutil device program`.
DFU_PKG_OPTS := --application-version 1 --hw-version 52 --sd-req 0x00

flash-blinky: build-blinky
	nrfutil nrf5sdk-tools pkg generate \
		--application build-blinky/zephyr/zephyr.hex $(DFU_PKG_OPTS) \
		build-blinky/zephyr/zephyr_dfu.zip
	nrfutil device program --firmware build-blinky/zephyr/zephyr_dfu.zip $(DFU_TRAITS)

flash-cdc: build-cdc
	nrfutil nrf5sdk-tools pkg generate \
		--application build-cdc/zephyr/zephyr.hex $(DFU_PKG_OPTS) \
		build-cdc/zephyr/zephyr_dfu.zip
	nrfutil device program --firmware build-cdc/zephyr/zephyr_dfu.zip $(DFU_TRAITS)

# ── Firmware (BLE Receiver) ───────────────────────────────────
FW_DIR := ble-receiver/firmware
FW_BUILD := build-fw
TEST ?= test_formatter_text
TEST_DIR := ble-receiver/tests/unit/$(TEST)
TEST_BUILD := build-test

build-fw:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b $(BOARD) $(FW_DIR) -d $(FW_BUILD) --no-sysbuild

flash-fw: build-fw
	nrfutil nrf5sdk-tools pkg generate \
		--application $(FW_BUILD)/zephyr/zephyr.hex $(DFU_PKG_OPTS) \
		$(FW_BUILD)/zephyr/zephyr_dfu.zip
	nrfutil device program --firmware $(FW_BUILD)/zephyr/zephyr_dfu.zip $(DFU_TRAITS)

# native_sim uses host GCC; NCS toolchain's older libmpfr shadows host's,
# breaking GCC 16+ (needs mpfr_asinpi from MPFR 4.2+). Prepend host lib path.
build-test:
	$(LAUNCH) bash -c 'LD_LIBRARY_PATH=/usr/lib:$$LD_LIBRARY_PATH ZEPHYR_BASE=$(ZEPHYR_BASE) west build -b native_sim $(TEST_DIR) -d $(TEST_BUILD) --no-sysbuild'

run-test:
	$(LAUNCH) bash -c 'LD_LIBRARY_PATH=/usr/lib:$$LD_LIBRARY_PATH ZEPHYR_BASE=$(ZEPHYR_BASE) west build -t run -d $(TEST_BUILD)'

clean-fw:
	rm -rf $(FW_BUILD)

clean-test:
	rm -rf $(TEST_BUILD)

# ── List connected devices ───────────────────────────────────
devices:
	nrfutil device list

# ── Clean ────────────────────────────────────────────────────
clean: clean-blinky clean-cdc clean-fw clean-test

clean-blinky:
	rm -rf build-blinky

clean-cdc:
	rm -rf build-cdc
