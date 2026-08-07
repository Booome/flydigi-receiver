# Wireless firmware (nRF52840 + Zephyr RTOS)
# Usage: cd wireless && make build / make flash / make test

NCS_VERSION := v3.4.0
BOARD := nrf52840dongle
LAUNCH := nrfutil sdk-manager toolchain launch --ncs-version $(NCS_VERSION) --
NCS_HOME := $(HOME)/ncs/$(NCS_VERSION)
ZEPHYR_BASE := $(NCS_HOME)/zephyr
DFU_TRAITS := --traits nordicDfu
DFU_PKG_OPTS := --application-version 1 --hw-version 52 --sd-req 0x00

FW_DIR := firmware
FW_BUILD := build
TEST ?= test_formatter_text
TEST_DIR := tests/unit/$(TEST)
TEST_BUILD := build-test

BLINKY_SRC := $(NCS_HOME)/zephyr/samples/basic/blinky
CDC_ACM_SRC := $(NCS_HOME)/zephyr/samples/subsys/usb/cdc_acm
BLINKY_BUILD := build-blinky
CDC_BUILD := build-cdc

.PHONY: build flash build-test run-test clean devices
.PHONY: build-blinky build-cdc flash-blinky flash-cdc

# ── Build firmware ────────────────────────────────────────────
build:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b $(BOARD) $(FW_DIR) -d $(FW_BUILD) --no-sysbuild

# ── Flash (requires Dongle in DFU mode) ──────────────────────
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

# ── NCS reference samples ────────────────────────────────────
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

# ── Utilities ────────────────────────────────────────────────
devices:
	nrfutil device list

clean:
	rm -rf $(FW_BUILD) $(TEST_BUILD) $(BLINKY_BUILD) $(CDC_BUILD)
