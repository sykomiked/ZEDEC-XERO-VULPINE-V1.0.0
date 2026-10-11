# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/engines/emu-state — deterministic lockstep simulation over libzxv.
#
#   make                  build/libzxv_emu.a and build/libzxv_emu.so (.dylib)
#   make test             unit tests, a fuzz smoke run, the demo and the
#                         no-float check
#   make demo             two peers, one predicting the other's late inputs
#   make fuzz             the deterministic fuzz driver (FUZZ_ITERS=...,
#                         ZXV_EMU_FUZZ_SEED=n for another stream)
#   make fuzz-libfuzzer   the libFuzzer target, if clang ships libFuzzer
#   make check-nofloat    R11: no float type in the library, tests or demo
#   make dist             build/dist/{include,lib}: what the engine kits ship
#   make clean
#
# Options:
#   SANITIZE=1            -fsanitize=address,undefined everywhere
#   OUT=dir               build directory (default build)

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../../libzxv
NOFLOAT_TOOL := ../../kits/z-wasm/tools/check_nofloat.py

ZXV_ABS := $(abspath $(ZXV)/build)
ifeq ($(shell uname -s),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
  SO      := $(OUT)/libzxv_emu.dylib
  SOFLAGS := -dynamiclib -install_name @rpath/libzxv_emu.dylib -Wl,-rpath,@loader_path
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
  SO      := $(OUT)/libzxv_emu.so
  SOFLAGS := -shared -Wl,-soname,libzxv_emu.so -Wl,-rpath,'$$ORIGIN'
  # every symbol resolved at link time (not under clang's sanitizers, whose
  # runtime is linked into the executable, not the shared library)
  ifeq ($(SANITIZE),)
    SOFLAGS += -Wl,-z,defs
  endif
endif

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_ := -std=c11 -O2 -g -fPIC -fvisibility=hidden $(WARN) -fstack-protector-strong $(SAN) \
           $(NOFLOAT) -DZXV_EMU_BUILD_SHARED -Iinclude -I$(ZXV)/include

SRC  := src/zxv_emu.c
OBJ  := $(OUT)/obj/zxv_emu.o
LIB  := $(OUT)/libzxv_emu.a
HDRS := include/zxv_emu.h $(ZXV)/include/zxv_api.h

LINK   := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) $(SAN)
TESTCC  = $(CC) -std=c11 -O1 -g $(WARN) -Wno-missing-prototypes $(SAN) -Iinclude -I$(ZXV)/include -Itest

FUZZ_ITERS ?= 20000

.PHONY: all test demo fuzz fuzz-libfuzzer check-nofloat dist clean

all: $(LIB) $(SO)

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(OBJ): $(SRC) $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(SO): $(OBJ) $(ZXV_LIB)
	$(CC) $(SOFLAGS) $(OBJ) -L$(ZXV_ABS) -lzxv $(SAN) -o $@

$(OUT)/test_emu: test/test_emu.c test/game.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/fuzz_emu: test/fuzz_emu.c test/game.h $(LIB) $(ZXV_LIB)
	$(TESTCC) -DZXV_EMU_FUZZ_MAIN $< $(LIB) $(LINK) -o $@

$(OUT)/lockstep_demo: examples/lockstep_demo.c test/game.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

test: check-nofloat all $(OUT)/test_emu $(OUT)/fuzz_emu $(OUT)/lockstep_demo
	$(OUT)/test_emu
	$(OUT)/fuzz_emu 2000
	$(OUT)/lockstep_demo > /dev/null

demo: $(OUT)/lockstep_demo
	$(OUT)/lockstep_demo

fuzz: $(OUT)/fuzz_emu
	$(OUT)/fuzz_emu $(FUZZ_ITERS)

fuzz-libfuzzer:
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  clang -std=c11 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude -I$(ZXV)/include -Itest \
	    test/fuzz_emu.c $(SRC) -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -o $(OUT)/fuzz_libfuzzer && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=60 -max_len=512; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

check-nofloat:
	$(PYTHON) $(NOFLOAT_TOOL) $(SRC) include/zxv_emu.h test/test_emu.c test/fuzz_emu.c test/game.h \
	  examples/lockstep_demo.c
	@echo "compiler guard for the library: $(or $(NOFLOAT),none on this compiler)"

dist: all
	@mkdir -p $(OUT)/dist/include $(OUT)/dist/lib
	cp include/zxv_emu.h $(ZXV)/include/zxv_api.h $(OUT)/dist/include/
	cp $(LIB) $(SO) $(OUT)/dist/lib/

clean:
	rm -rf $(OUT)
