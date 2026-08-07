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

# ── Directories ───────────────────────────────────────────────
WIRELESS_DIR := wireless
FW_DIR := $(WIRELESS_DIR)/firmware
FW_BUILD := $(WIRELESS_DIR)/build
TEST ?= test_formatter_text
TEST_DIR := $(WIRELESS_DIR)/tests/unit/$(TEST)
TEST_BUILD := $(WIRELESS_DIR)/build-test

# NCS sample apps (for environment verification)
BLINKY_SRC := $(NCS_HOME)/zephyr/samples/basic/blinky
CDC_ACM_SRC := $(NCS_HOME)/zephyr/samples/subsys/usb/cdc_acm
REF_DIR := reference
BLINKY_BUILD := $(REF_DIR)/build-blinky
CDC_BUILD := $(REF_DIR)/build-cdc

.PHONY: build flash build-test run-test
.PHONY: build-blinky build-cdc flash-blinky flash-cdc
.PHONY: clean clean-fw clean-test clean-blinky clean-cdc devices

# ── Wireless firmware ─────────────────────────────────────────
# This project lives outside the NCS folder hierarchy, so west
# cannot auto-discover the workspace. Setting ZEPHYR_BASE lets
# west locate the Zephyr tree and load extension commands (like
# `build`). See:
#   sdk-nrf/doc/nrf/app_dev/create_application.rst (ZEPHYR_BASE)
build:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b $(BOARD) $(FW_DIR) -d $(FW_BUILD) --no-sysbuild

# ── Flash (requires Dongle in DFU mode) ──────────────────────
# nRF52840 Dongle uses Nordic secure DFU, which requires a SdfuZip
# package (not a raw .hex). The zip is generated from .hex by
# `nrfutil nrf5sdk-tools pkg generate`, then flashed with
# `nrfutil device program`.
DFU_PKG_OPTS := --application-version 1 --hw-version 52 --sd-req 0x00

flash: build
	nrfutil nrf5sdk-tools pkg generate \
		--application $(FW_BUILD)/zephyr/zephyr.hex $(DFU_PKG_OPTS) \
		$(FW_BUILD)/zephyr/zephyr_dfu.zip
	nrfutil device program --firmware $(FW_BUILD)/zephyr/zephyr_dfu.zip $(DFU_TRAITS)

# ── Unit tests (native_sim) ──────────────────────────────────
# native_sim uses host GCC; NCS toolchain's older libmpfr shadows host's,
# breaking GCC 16+ (needs mpfr_asinpi from MPFR 4.2+). Prepend host lib path.
build-test:
	$(LAUNCH) bash -c 'LD_LIBRARY_PATH=/usr/lib:$$LD_LIBRARY_PATH ZEPHYR_BASE=$(ZEPHYR_BASE) west build -b native_sim $(TEST_DIR) -d $(TEST_BUILD) --no-sysbuild'

run-test:
	$(LAUNCH) bash -c 'LD_LIBRARY_PATH=/usr/lib:$$LD_LIBRARY_PATH ZEPHYR_BASE=$(ZEPHYR_BASE) west build -t run -d $(TEST_BUILD)'

# ── Reference (NCS sample verification) ──────────────────────
build-blinky:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b $(BOARD) $(BLINKY_SRC) -d $(BLINKY_BUILD) --no-sysbuild

build-cdc:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b $(BOARD) $(CDC_ACM_SRC) -d $(CDC_BUILD) --no-sysbuild

flash-blinky: build-blinky
	nrfutil nrf5sdk-tools pkg generate \
		--application $(BLINKY_BUILD)/zephyr/zephyr.hex $(DFU_PKG_OPTS) \
		$(BLINKY_BUILD)/zephyr/zephyr_dfu.zip
	nrfutil device program --firmware $(BLINKY_BUILD)/zephyr/zephyr_dfu.zip $(DFU_TRAITS)

flash-cdc: build-cdc
	nrfutil nrf5sdk-tools pkg generate \
		--application $(CDC_BUILD)/zephyr/zephyr.hex $(DFU_PKG_OPTS) \
		$(CDC_BUILD)/zephyr/zephyr_dfu.zip
	nrfutil device program --firmware $(CDC_BUILD)/zephyr/zephyr_dfu.zip $(DFU_TRAITS)

# ── List connected devices ───────────────────────────────────
devices:
	nrfutil device list

# ── Clean ────────────────────────────────────────────────────
clean: clean-fw clean-test clean-blinky clean-cdc

clean-fw:
	rm -rf $(FW_BUILD)

clean-test:
	rm -rf $(TEST_BUILD)

clean-blinky:
	rm -rf $(BLINKY_BUILD)

clean-cdc:
	rm -rf $(CDC_BUILD)
