# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/industry/v-band — low-power state logging for LoRaWAN / NB-IoT
# sensors (MCU subset core, host-compiled tests). Targets and options:
# ../mk/kit.mk, plus:
#
#   check-mcu   compile the core freestanding (-ffreestanding -fno-builtin,
#               no stack protector) and fail if it references any symbol
#               outside libzxv's pure functions and the four memory
#               primitives a compiler may emit (memcpy memset memmove
#               memcmp). Run by `make test`.
#   size-arm    if arm-none-eabi-gcc is installed: build the core and
#               tools/mcu_probe.c for a Cortex-M4 (-Os, soft float) and print
#               flash/RAM from arm-none-eabi-size, plus the deepest stack
#               frame. Skipped with a message otherwise. Run by `make test`.

KIT      := v-band
LIBNAME  := zvband
SRC      := src/zvband.c
HDRS     := include/zvband.h
TESTS    := test_vband
FUZZ     := fuzz_vband
EXAMPLES := sensor_demo
FUZZ_SMOKE ?= 1500

include ../mk/kit.mk

ARM_CC   ?= arm-none-eabi-gcc
ARM_SIZE ?= arm-none-eabi-size
MCU_ALLOW := ^_?(zxv_[a-z0-9_]+|memcpy|memset|memmove|memcmp)$$

.PHONY: check-mcu size-arm
test: check-mcu size-arm

check-mcu: $(SRC) $(HDRS)
	@mkdir -p $(OUT)/mcu
	@$(CC) -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic \
	  $(WARN) $(NOFLOAT) -Iinclude -I$(ZXV)/include -c src/zvband.c -o $(OUT)/mcu/zvband.o
	@undef=$$(nm -u $(OUT)/mcu/zvband.o | awk '{print $$NF}'); \
	 bad=$$(printf '%s\n' $$undef | grep -Ev '$(MCU_ALLOW)' || true); \
	 if [ -n "$$bad" ]; then echo "$(KIT): core calls outside the MCU subset: $$bad"; exit 1; fi; \
	 echo "$(KIT): MCU subset ok, external symbols:" $$undef

size-arm: $(SRC) $(HDRS) tools/mcu_probe.c
	@if command -v $(ARM_CC) >/dev/null 2>&1; then \
	  mkdir -p $(OUT)/arm && \
	  $(ARM_CC) -std=c11 -Os -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -ffreestanding \
	    -ffunction-sections -fdata-sections -fstack-usage $(WARN) -Iinclude -I$(ZXV)/include \
	    -c src/zvband.c -o $(OUT)/arm/zvband.o && \
	  $(ARM_CC) -std=c11 -Os -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -ffreestanding \
	    -fstack-usage $(WARN) -Iinclude -I$(ZXV)/include \
	    -c tools/mcu_probe.c -o $(OUT)/arm/mcu_probe.o && \
	  $(ARM_SIZE) $(OUT)/arm/zvband.o $(OUT)/arm/mcu_probe.o && \
	  echo "$(KIT): deepest frames (bytes):" && \
	  cat $(OUT)/arm/*.su | sort -t'	' -k2 -n -r | head -3; \
	else echo "$(KIT): $(ARM_CC) not installed, cross size check skipped"; fi
