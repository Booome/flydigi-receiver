# NCS sample apps verification (blinky / cdc_acm)
# Usage: cd reference && make build-blinky / make build-cdc

NCS_VERSION := v3.4.0
BOARD := nrf52840dongle
LAUNCH := nrfutil sdk-manager toolchain launch --ncs-version $(NCS_VERSION) --
NCS_HOME := $(HOME)/ncs/$(NCS_VERSION)
ZEPHYR_BASE := $(NCS_HOME)/zephyr
DFU_TRAITS := --traits nordicDfu
DFU_PKG_OPTS := --application-version 1 --hw-version 52 --sd-req 0x00

BLINKY_SRC := $(NCS_HOME)/zephyr/samples/basic/blinky
CDC_ACM_SRC := $(NCS_HOME)/zephyr/samples/subsys/usb/cdc_acm
BLINKY_BUILD := build-blinky
CDC_BUILD := build-cdc

.PHONY: build-blinky build-cdc flash-blinky flash-cdc clean devices

# ── Build ─────────────────────────────────────────────────────
build-blinky:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b $(BOARD) $(BLINKY_SRC) -d $(BLINKY_BUILD) --no-sysbuild

build-cdc:
	ZEPHYR_BASE=$(ZEPHYR_BASE) $(LAUNCH) west build -b $(BOARD) $(CDC_ACM_SRC) -d $(CDC_BUILD) --no-sysbuild

# ── Flash (requires Dongle in DFU mode) ──────────────────────
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
	rm -rf $(BLINKY_BUILD) $(CDC_BUILD)
