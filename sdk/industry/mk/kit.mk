# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/industry/mk/kit.mk — the shared build for every industry kit. A kit's
# Makefile sets a few variables and includes this file:
#
#   KIT        short name, for messages               (e.g. logix-clear)
#   LIBNAME    archive name: $(OUT)/lib$(LIBNAME).a    (e.g. zlogix)
#   SRC        the kit's sources                      (src/*.c)
#   HDRS       headers the objects depend on          (include/*.h src/*.h)
#   TESTS      test programs, test/NAME.c -> $(OUT)/NAME, each run by `make test`
#   FUZZ       the fuzz program, test/NAME.c (deterministic; ZIND_FUZZ_SEED=n
#              picks another stream)
#   EXAMPLES   example programs, examples/NAME.c, run by `make test` and `make demo`
#   FUZZ_SMOKE fuzz iterations run by `make test` (default 2000)
#
# Targets: all, test, fuzz (FUZZ_ITERS=...), demo, check-nofloat, clean.
#
# Options:
#   CC=clang       any C11 compiler (default gcc)
#   SANITIZE=1     -fsanitize=address,undefined for the kit, the shared layer
#                  and the tests (libzxv itself is used as built)
#   OUT=dir        build directory (default build; CI uses build/san for SANITIZE)
#
# Kits link libzxv (sdk/libzxv/build) and the shared layer (sdk/industry/
# common), compiled into $(OUT)/zind with the same flags as the kit. Nothing
# here reaches kernel/.

ifeq ($(origin CC),default)
  CC := gcc
endif
AR         ?= ar
PYTHON     ?= python3
OUT        ?= build
FUZZ_ITERS ?= 20000
FUZZ_SMOKE ?= 2000

IND     := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)
ZXV     := $(abspath $(IND)/../libzxv)
COMMON  := $(IND)/common
NOFLOAT_PY := $(abspath $(IND)/../kits/z-wasm/tools/check_nofloat.py)

ZXV_ABS := $(abspath $(ZXV)/build)
ifeq ($(shell uname -s),Darwin)
  ZXV_LIB := $(ZXV_ABS)/libzxv.dylib
else
  ZXV_LIB := $(ZXV_ABS)/libzxv.so
endif

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif
# R11 compiler guard where the target has one (x86-64, aarch64 gcc/clang)
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
INCS    := -Iinclude -Isrc -I$(COMMON)/include -I$(ZXV)/include -I$(COMMON)/test
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) $(INCS)
TESTCC   = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) $(INCS)
LINK    := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread $(SAN)

ZIND_SRC  := $(wildcard $(COMMON)/src/*.c)
ZIND_HDRS := $(COMMON)/include/zind_common.h $(ZXV)/include/zxv_api.h
ZIND_OBJ  := $(patsubst $(COMMON)/src/%.c,$(OUT)/zind/%.o,$(ZIND_SRC))
ZIND_LIB  := $(OUT)/zind/libzind.a

OBJ  := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRC))
LIB  := $(OUT)/lib$(LIBNAME).a
ifeq ($(LIBNAME),zind)
  LIBS := $(ZIND_LIB)
else
  LIBS := $(LIB) $(ZIND_LIB)
endif
DEPS := $(HDRS) $(ZIND_HDRS) $(COMMON)/test/zind_test.h

TEST_BINS    := $(addprefix $(OUT)/,$(TESTS))
FUZZ_BIN     := $(if $(FUZZ),$(OUT)/$(FUZZ))
EXAMPLE_BINS := $(addprefix $(OUT)/,$(EXAMPLES))

.PHONY: all test fuzz demo check-nofloat clean

all: $(LIBS) $(EXAMPLE_BINS)

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(OUT)/zind/%.o: $(COMMON)/src/%.c $(ZIND_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(ZIND_LIB): $(ZIND_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/obj/%.o: src/%.c $(DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

ifneq ($(LIBNAME),zind)
$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^
endif

$(OUT)/%: test/%.c $(LIBS) $(ZXV_LIB) $(DEPS)
	@mkdir -p $(OUT)
	$(TESTCC) $< $(LIBS) $(LINK) -o $@

$(OUT)/%: examples/%.c $(LIBS) $(ZXV_LIB) $(DEPS)
	@mkdir -p $(OUT)
	$(TESTCC) $< $(LIBS) $(LINK) -o $@

test: check-nofloat $(TEST_BINS) $(FUZZ_BIN) $(EXAMPLE_BINS)
	@set -e; for t in $(TEST_BINS); do $$t; done
	$(if $(FUZZ_BIN),$(FUZZ_BIN) $(FUZZ_SMOKE))
	@set -e; for e in $(EXAMPLE_BINS); do $$e > /dev/null; echo "$$e: ok"; done

fuzz: $(FUZZ_BIN)
	$(if $(FUZZ_BIN),$(FUZZ_BIN) $(FUZZ_ITERS),@echo "$(KIT): no fuzz target")

demo: $(EXAMPLE_BINS)
	@set -e; for e in $(EXAMPLE_BINS); do $$e; done

check-nofloat:
	@$(PYTHON) $(NOFLOAT_PY) $(SRC) $(HDRS) $(wildcard test/*.c test/*.h examples/*.c) \
	  $(ZIND_SRC) $(COMMON)/include/zind_common.h $(COMMON)/test/zind_test.h
	@echo "$(KIT): compiler guard: $(or $(NOFLOAT),none on this compiler)"

clean:
	rm -rf $(OUT)
