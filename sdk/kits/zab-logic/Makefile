# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/kits/zab-logic — per-cycle token budgets and a hexagram decision gate
# for a swarm of agents, on the public libzxv ABI.
#
#   make                 build/libzab.a
#   make test            unit tests, a fuzz smoke run, the demo and the
#                        no-float check
#   make demo            three levels of agents over three cycles
#   make fuzz            the deterministic fuzzer (FUZZ_ITERS=...,
#                        ZAB_FUZZ_SEED=n for another stream)
#   make check-nofloat   R11: no float type in the kit, tests or demo
#   make clean
#
# Options:
#   SANITIZE=1           -fsanitize=address,undefined everywhere
#   OUT=dir              build directory (default build)
#
# The kit needs only a C11 compiler and libzxv. Python is used for one thing,
# the shared no-float source scan (sdk/kits/z-wasm/tools/check_nofloat.py);
# without python3 that scan is skipped and the compiler guard below
# (-mgeneral-regs-only, where the compiler has it) still applies.

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../../libzxv
NOFLOAT_TOOL := ../z-wasm/tools/check_nofloat.py

ZXV_ABS := $(abspath $(ZXV)/build)
ifeq ($(shell uname -s),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
endif

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wconversion \
           -Wno-sign-conversion
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) \
           -Iinclude -I$(ZXV)/include
TESTCC   = $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow $(SAN) -Iinclude -I$(ZXV)/include -Itest
LINK    := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread $(SAN)

SRC  := src/zab_logic.c
LIB  := $(OUT)/libzab.a
HDRS := include/zab_logic.h $(ZXV)/include/zxv_api.h
THDR := test/zab_test.h

FUZZ_ITERS ?= 20000

.PHONY: all test demo fuzz check-nofloat clean

all: $(LIB)

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(OUT)/zab_logic.o: $(SRC) $(HDRS)
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OUT)/zab_logic.o
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/test_zab: test/test_zab.c $(THDR) $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/fuzz_zab: test/fuzz_zab.c $(THDR) $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/three_levels: examples/three_levels.c $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

test: check-nofloat $(OUT)/test_zab $(OUT)/fuzz_zab $(OUT)/three_levels
	$(OUT)/test_zab
	$(OUT)/fuzz_zab 500
	$(OUT)/three_levels > /dev/null

demo: $(OUT)/three_levels
	$(OUT)/three_levels

fuzz: $(OUT)/fuzz_zab
	$(OUT)/fuzz_zab $(FUZZ_ITERS)

check-nofloat:
	@if command -v $(PYTHON) >/dev/null 2>&1; then \
	  $(PYTHON) $(NOFLOAT_TOOL) $(SRC) include/zab_logic.h test/*.c test/*.h examples/*.c; \
	else echo "check-nofloat: $(PYTHON) not found, source scan skipped"; fi
	@echo "compiler guard for the kit source: $(or $(NOFLOAT),none on this compiler)"

clean:
	rm -rf $(OUT)
