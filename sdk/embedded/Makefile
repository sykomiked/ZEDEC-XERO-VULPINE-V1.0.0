# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/embedded — libzxv on microcontrollers and gateways: the port layer
# (port/), the fixed-memory ledger and phase-clock driver (zxe/), and the
# FreeRTOS and Zephyr integrations (freertos-zxv/, zephyr-zxv/).
#
#   make test            host tests: the port layer (twice: TLS and
#                        ZXP_NO_TLS), the MCU subset through the port, the
#                        zxe ledger (unit + differential against libzxv),
#                        libzxv's own conformance tests linked against the
#                        port build, and the source no-float check
#   make cross TARGET=t  cross-build the subset for t (see TARGETS) into
#                        build/t, with the bare-metal port and size probes
#   make size            cross-build every target whose compiler is
#                        installed; write build/size-report.md
#   make check-nofloat   R11 on our sources; and, after `make size`, the
#                        binary check of every cross object (soft-float
#                        helper calls are allowed only in the documented
#                        ledger-mirror objects of the exact profile)
#   make clean
#
# Options: SANITIZE=1 (host tests), ZXE_PROFILE=exact|q32, ZXE_PQC=1 (cross),
#          FUZZ_OPS=n (differential operations per seed, default 20000)

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../libzxv
FUZZ_OPS ?= 20000
ZXE_PROFILE ?= exact

export ZXE_PROFILE

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all \
         -fno-omit-frame-pointer
endif

NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

HOST     := $(OUT)/host
HOSTNT   := $(OUT)/host-notls
HOSTFULL := $(OUT)/host-full
LIBMK    := $(MAKE) --no-print-directory -f lib.mk

TESTCC = $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror $(SAN) -Itest -Iport/include -Izxe/include \
         -I$(ZXV)/include
HOSTLIBS = $(1)/libzxe.a $(1)/libzxv_embedded.a $(1)/libzxp.a -lpthread $(SAN)

.PHONY: all test host-libs host-tests conformance cross size check-nofloat check-nofloat-src \
        check-nofloat-bin check-lists clean FORCE

all: host-libs

# ---- host ---------------------------------------------------------------

host-libs: FORCE
	$(LIBMK) CC=$(CC) AR=$(AR) OUT=$(HOST) PORT=posix TESTING=1 SANITIZE=$(SANITIZE) \
	  NOFLOAT_FLAG=$(NOFLOAT) OPT=-O1
	$(LIBMK) CC=$(CC) AR=$(AR) OUT=$(HOSTNT) PORT=posix TESTING=1 NO_TLS=1 SANITIZE=$(SANITIZE) \
	  NOFLOAT_FLAG=$(NOFLOAT) OPT=-O1 $(HOSTNT)/libzxp.a

$(HOST)/test_port: test/test_port.c test/zxe_test.h host-libs
	$(TESTCC) -DZXP_TESTING $< $(HOST)/libzxp.a -lpthread $(SAN) -o $@
$(HOST)/test_port_notls: test/test_port.c test/zxe_test.h host-libs
	$(TESTCC) -DZXP_TESTING -DZXP_NO_TLS $< $(HOSTNT)/libzxp.a -lpthread $(SAN) -o $@
$(HOST)/test_subset: test/test_subset.c test/zxe_test.h host-libs
	$(TESTCC) $< $(call HOSTLIBS,$(HOST)) -o $@
$(HOST)/test_zxe_ledger: test/test_zxe_ledger.c test/zxe_test.h host-libs
	$(TESTCC) -DZXE_TESTING $< $(call HOSTLIBS,$(HOST)) -o $@

host-tests: $(HOST)/test_port $(HOST)/test_port_notls $(HOST)/test_subset $(HOST)/test_zxe_ledger
	$(HOST)/test_port
	$(HOST)/test_port_notls
	$(HOST)/test_subset
	$(HOST)/test_zxe_ledger $(FUZZ_OPS)

# libzxv's own C conformance tests (sdk/libzxv/test), unchanged, linked
# against the full libzxv (PQC and engine on) compiled through the port
# layer, so every allocation, lock and random byte goes through zxp.
CONF_TESTS := test_zxv_api test_zxv_abi11 test_zxv_abi12 test_zxv_abi13
conformance: FORCE
	+$(LIBMK) CC=$(CC) AR=$(AR) OUT=$(HOSTFULL) PORT=posix ZXE_PQC=1 ZXE_TENSOR=1 OPT=-O2 \
	  SANITIZE=$(SANITIZE) NOFLOAT_FLAG=$(NOFLOAT)
	$(MAKE) -C $(ZXV) OUT=build SANITIZE= fixtures
	@for t in $(CONF_TESTS); do \
	  echo "$(CC) ... $(ZXV)/test/$$t.c test/autoinit.c -> $(HOSTFULL)/$$t"; \
	  $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror $(SAN) -I$(ZXV)/include -Iport/include \
	    $(ZXV)/test/$$t.c test/autoinit.c $(call HOSTLIBS,$(HOSTFULL)) -o $(HOSTFULL)/$$t || exit 1; \
	done
	$(HOSTFULL)/test_zxv_api
	$(HOSTFULL)/test_zxv_abi11 ../conformance/fixtures
	$(HOSTFULL)/test_zxv_abi12 ../conformance/fixtures
	$(HOSTFULL)/test_zxv_abi13

test: check-nofloat-src check-lists host-tests conformance

# ---- cross --------------------------------------------------------------

TARGETS := cortex-m0plus cortex-m4 cortex-m33 rv32imac

CROSS_cortex-m0plus := arm-none-eabi-
CROSS_cortex-m4     := arm-none-eabi-
CROSS_cortex-m33    := arm-none-eabi-
CROSS_rv32imac      := riscv64-unknown-elf-
FLAGS_cortex-m0plus := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
FLAGS_cortex-m4     := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
FLAGS_cortex-m33    := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
FLAGS_rv32imac      := -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medany
LINK_cortex-m0plus  := --specs=nano.specs --specs=nosys.specs -Wl,--no-warnings
LINK_cortex-m4      := --specs=nano.specs --specs=nosys.specs -Wl,--no-warnings
LINK_cortex-m33     := --specs=nano.specs --specs=nosys.specs -Wl,--no-warnings
LINK_rv32imac       := --specs=picolibc.specs -Wl,--defsym=__flash=0x20000000 \
                       -Wl,--defsym=__flash_size=0x400000 -Wl,--defsym=__ram=0x80000000 \
                       -Wl,--defsym=__ram_size=0x100000 -Wl,--defsym=__stack_size=0x2000
# picolibc's headers on rv32 (newlib's come with nano.specs on arm)
CINC_rv32imac       := --specs=picolibc.specs

PROBES := pure core ledger $(if $(filter 1,$(ZXE_PQC)),pqc)
TARGET ?= cortex-m4
XO = $(OUT)/$(TARGET)

cross: FORCE
	@command -v $(CROSS_$(TARGET))gcc >/dev/null || { echo "$(CROSS_$(TARGET))gcc not installed"; exit 1; }
	$(LIBMK) CC=$(CROSS_$(TARGET))gcc AR=$(CROSS_$(TARGET))ar OUT=$(XO) PORT=baremetal NO_TLS=1 \
	  TARGET_CFLAGS="$(FLAGS_$(TARGET)) $(CINC_$(TARGET))" ZXE_PQC=$(ZXE_PQC)
	@for p in $(PROBES); do \
	  P=$$(echo $$p | tr a-z A-Z); \
	  $(CROSS_$(TARGET))gcc -std=c11 -Os -g $(FLAGS_$(TARGET)) -ffunction-sections -fdata-sections \
	    -DPROBE_$$P -Iport/include -Izxe/include -I$(ZXV)/include examples/size_probe.c \
	    $(XO)/libzxe.a $(XO)/libzxv_embedded.a $(XO)/libzxp.a $(LINK_$(TARGET)) \
	    -Wl,--gc-sections -Wl,-Map,$(XO)/probe_$$p.map -o $(XO)/probe_$$p.elf || exit 1; \
	done
	$(CROSS_$(TARGET))size $(XO)/probe_*.elf

size: FORCE
	@rm -f $(OUT)/size-targets.txt; mkdir -p $(OUT)
	@for t in $(TARGETS); do \
	  if command -v $$(make -s --no-print-directory print-cross T=$$t)gcc >/dev/null 2>&1; then \
	    $(MAKE) --no-print-directory cross TARGET=$$t || exit 1; echo $$t >> $(OUT)/size-targets.txt; \
	  else echo "skipping $$t: compiler not installed"; fi; \
	done
	$(PYTHON) tools/size_report.py $(OUT) $$(cat $(OUT)/size-targets.txt) > $(OUT)/size-report.md
	@cat $(OUT)/size-report.md

print-cross:
	@echo $(CROSS_$(T))

# ---- checks -------------------------------------------------------------

OWN_SRC := port/src/zxp_core.c port/src/zxp_posix.c port/src/zxp_baremetal.c \
           port/src/zxp_freertos.c port/src/zxp_zephyr.c port/src/zxp_os.h \
           port/include/zxp.h port/include/zxp_posix.h port/include/zxp_baremetal.h \
           port/include/zxp_zephyr.h port/compat/zxp_prelude.h port/compat/pthread.h \
           zxe/include/zxe.h zxe/src/zxe_ledger.c zxe/src/zxe_clock.c examples/size_probe.c \
           freertos-zxv/zxv_task.c freertos-zxv/zxv_task.h freertos-zxv/main_posix.c \
           zephyr-zxv/samples/ledger_budget/src/main.c $(wildcard test/*.c test/*.h) test/qemu/startup.c

check-nofloat-src:
	$(PYTHON) ../kits/z-wasm/tools/check_nofloat.py $(OWN_SRC)
	@echo "compiler guard for our sources on this host: $(or $(NOFLOAT),none)"

check-nofloat-bin:
	$(PYTHON) tools/check_nofloat_bin.py --profile $(ZXE_PROFILE) $(OUT) $$(cat $(OUT)/size-targets.txt)

check-nofloat: check-nofloat-src
	@if [ -f $(OUT)/size-targets.txt ]; then $(MAKE) --no-print-directory check-nofloat-bin; \
	 else echo "no cross build yet: run make size for the binary check"; fi

check-lists:
	$(PYTHON) tools/check_lists.py

clean:
	rm -rf $(OUT) freertos-zxv/build zephyr-zxv/build

FORCE:

# ---- emulated target ----------------------------------------------------
# The MCU subset on QEMU's mps2-an385 (Cortex-M3) and mps2-an386 (Cortex-M4)
# with newlib semihosting. Needs arm-none-eabi-gcc, libnewlib-arm-none-eabi
# and qemu-system-arm.
QEMU_BOARDS := an385:cortex-m3 an386:cortex-m4

qemu-test: FORCE
	@for bc in $(QEMU_BOARDS); do \
	  b=$${bc%%:*}; c=$${bc##*:}; o=$(OUT)/qemu-$$c; \
	  $(LIBMK) CC=arm-none-eabi-gcc AR=arm-none-eabi-ar OUT=$$o PORT=baremetal NO_TLS=1 \
	    TARGET_CFLAGS="-mcpu=$$c -mthumb -mfloat-abi=soft" >/dev/null || exit 1; \
	  arm-none-eabi-gcc -std=c11 -O1 -g -mcpu=$$c -mthumb -mfloat-abi=soft -Wall -Wextra -Werror \
	    -Iport/include -Izxe/include -I$(ZXV)/include test/test_target.c test/qemu/startup.c \
	    $$o/libzxe.a $$o/libzxv_embedded.a $$o/libzxp.a --specs=rdimon.specs -nostartfiles \
	    -T test/qemu/mps2.ld -Wl,--gc-sections -Wl,--no-warnings -o $$o/test_target.elf || exit 1; \
	  echo "qemu-system-arm -M mps2-$$b ($$c): $$o/test_target.elf"; \
	  timeout 120 qemu-system-arm -M mps2-$$b -nographic -monitor none -serial none \
	    -semihosting-config enable=on,target=native -kernel $$o/test_target.elf > $$o/qemu.log 2>&1; \
	  cat $$o/qemu.log; grep -q "test_target: [0-9]* checks, 0 failures" $$o/qemu.log || exit 1; \
	done
