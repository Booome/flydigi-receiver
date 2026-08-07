# Flydigi Receiver - top-level Makefile
# Build targets are in subdirectory Makefiles.

.PHONY: help devices

help:
	@echo "Flydigi Receiver - build targets"
	@echo ""
	@echo "  Wireless (2.4GHz, nRF52840):"
	@echo "    make -C wireless build          Build firmware"
	@echo "    make -C wireless flash           Build + flash (DFU mode)"
	@echo "    make -C wireless build-test      Build unit tests"
	@echo "    make -C wireless run-test        Run unit tests"
	@echo "    make -C wireless build-blinky    Build NCS blinky sample"
	@echo "    make -C wireless build-cdc       Build NCS CDC ACM sample"
	@echo "    make -C wireless flash-blinky    Flash blinky"
	@echo "    make -C wireless flash-cdc       Flash CDC ACM"
	@echo "    make -C wireless clean           Clean all build outputs"
	@echo ""
	@echo "  Global:"
	@echo "    make devices                     List connected Nordic devices"

devices:
	nrfutil device list
