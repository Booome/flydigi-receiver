# nRF Connect SDK (NCS) Build System
# All NCS environment vars are injected at build time by
# `nrfutil sdk-manager toolchain launch`, keeping the global
# shell environment clean.

NCS_VERSION := v3.4.0
BOARD := nrf52840dongle
LAUNCH := nrfutil sdk-manager toolchain launch --ncs-version $(NCS_VERSION) --
NCS_HOME := $(HOME)/ncs/$(NCS_VERSION)
DFU_TRAITS := --traits nordicDfu

# ── Sample apps (in NCS tree, for verification) ──────────────
BLINKY_SRC := $(NCS_HOME)/zephyr/samples/basic/blinky
CDC_ACM_SRC := $(NCS_HOME)/zephyr/samples/subsys/usb/cdc_acm

.PHONY: build-blinky build-cdc flash-blinky flash-cdc
.PHONY: clean clean-blinky clean-cdc devices

# ── Build ────────────────────────────────────────────────────
build-blinky:
	$(LAUNCH) west build -b $(BOARD) $(BLINKY_SRC) -d build-blinky --no-sysbuild

build-cdc:
	$(LAUNCH) west build -b $(BOARD) $(CDC_ACM_SRC) -d build-cdc --no-sysbuild

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

# ── List connected devices ───────────────────────────────────
devices:
	nrfutil device list

# ── Clean ────────────────────────────────────────────────────
clean: clean-blinky clean-cdc

clean-blinky:
	rm -rf build-blinky

clean-cdc:
	rm -rf build-cdc
