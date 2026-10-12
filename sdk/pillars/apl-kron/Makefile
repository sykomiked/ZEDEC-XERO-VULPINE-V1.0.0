# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/pillars/apl-kron — APL and K -> KRON, exact array expressions over libzxv.
#
#   make                 build/libzkron.a and build/zkron (the command-line tool)
#   make test            C unit tests, golden programs (expected values from
#                        Python fractions), glyph/ASCII equivalence, a fuzz
#                        smoke run and the no-float check
#   make bindings        the ngn/k and GNU APL bindings and their tests, when
#                        the interpreters are given (NGNK=... GNUAPL_SRC=...)
#   make demo            a few programs through the zkron tool
#   make fuzz            the deterministic parser/evaluator fuzzer (FUZZ_ITERS=...)
#   make fuzz-libfuzzer  the libFuzzer target, if clang ships libFuzzer
#   make check-nofloat   R11: no float type in the C sources
#   make clean
#
# Options:
#   SANITIZE=1           -fsanitize=address,undefined
#   OUT=dir              build directory (default build/, git-ignored)
#   NGNK=path/to/k       an ngn/k built with -Wl,-E (for `2:` extensions), and
#   NGNK_SRC=dir         its source tree (for k.h)
#   GNUAPL=path/to/apl   GNU APL 1.8+ and GNUAPL_SRC=dir, its configured source
#                        tree (for Native_interface.hh)
#
# libzxv is built with `make -C ../../libzxv all`.

ifeq ($(origin CC),default)
  CC := gcc
endif
ifeq ($(origin CXX),default)
  CXX := g++
endif
AR      ?= ar
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../../libzxv
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
  SHARED  := -dynamiclib -undefined dynamic_lookup
  SOEXT   := dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
  SHARED  := -shared
  SOEXT   := so
endif
ZXV_ABS := $(abspath $(ZXV)/build)

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif

# R11 compiler guard where the compiler enforces it (x86-64 and arm64 gcc/clang).
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) \
           -Iinclude -Isrc -I$(ZXV)/include

SRC  := src/kr_util.c src/kr_parse.c src/kr_print.c src/kr_value.c src/kr_eval.c src/kr_run.c
OBJ  := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRC))
LIB  := $(OUT)/libzkron.a
HDRS := include/zkron.h src/kr_internal.h $(ZXV)/include/zxv_api.h

LINK  := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread $(SAN)
TESTCC = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) -Iinclude -Isrc -I$(ZXV)/include -Itest

CTESTS := test_parse test_eval test_post
FUZZ_ITERS ?= 200000
LIBFUZZER_SECONDS ?= 60
GOLDEN_SEED ?= 20260101
GOLDEN_RANDOM ?= 3000

.PHONY: all test golden fuzz fuzz-libfuzzer check-nofloat bindings bindings-k bindings-apl demo clean

all: $(LIB) $(OUT)/zkron

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/zkron: tools/zkron_tool.c $(LIB) $(ZXV_LIB) $(HDRS)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_%: test/test_%.c test/kr_test.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/fuzz_parse: test/fuzz_parse.c $(LIB) $(ZXV_LIB)
	$(TESTCC) -DKR_FUZZ_MAIN $< $(LIB) $(LINK) -o $@

# Expected values are computed by tools/gen_golden.py with Python's
# fractions module, from Python expressions written next to each program
# and from an independent Python model for the random cases; never by
# libzkron itself.
$(OUT)/golden/.stamp: tools/gen_golden.py $(wildcard test/golden/*.kg)
	@mkdir -p $(OUT)/golden
	$(PYTHON) tools/gen_golden.py --out $(OUT)/golden --seed $(GOLDEN_SEED) --random $(GOLDEN_RANDOM) test/golden/*.kg
	@touch $@

golden: $(OUT)/zkron $(OUT)/golden/.stamp
	$(OUT)/zkron golden $(OUT)/golden/*.kgold

test: check-nofloat $(addprefix $(OUT)/,$(CTESTS)) $(OUT)/fuzz_parse $(OUT)/zkron golden
	@for t in $(CTESTS); do $(OUT)/$$t || exit 1; done
	$(OUT)/fuzz_parse 20000 examples/*.apl examples/*.k
	$(OUT)/zkron same examples/stats.apl examples/stats.k
	$(OUT)/zkron same examples/matrix.apl examples/matrix.k
	@if $(OUT)/zkron same examples/fold.apl examples/fold.k >/dev/null; then \
	  echo "fold.apl and fold.k must differ (APL right fold vs K left fold)"; exit 1; fi
	$(OUT)/zkron check examples/*.apl examples/*.k
	@if $(OUT)/zkron check examples/broken.kr 2>/dev/null; then echo "broken.kr parsed"; exit 1; fi
	@echo "apl-kron: all tests passed"

demo: $(OUT)/zkron
	$(OUT)/zkron eval examples/stats.apl
	$(OUT)/zkron --display eval examples/matrix.k
	$(OUT)/zkron canon examples/stats.k
	$(OUT)/zkron same examples/stats.apl examples/stats.k
	$(OUT)/zkron eval -e '+/10⍴0.1'
	$(OUT)/zkron eval -e '×/1+⍳20'
	-$(OUT)/zkron eval -e '×/1+⍳21'

fuzz: $(OUT)/fuzz_parse
	$(OUT)/fuzz_parse $(FUZZ_ITERS) examples/*.apl examples/*.k

fuzz-libfuzzer:
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  mkdir -p $(OUT)/corpus && cp examples/*.apl examples/*.k $(OUT)/corpus/ && \
	  clang -std=gnu11 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude -Isrc \
	    -I$(ZXV)/include test/fuzz_parse.c $(SRC) \
	    -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -o $(OUT)/fuzz_libfuzzer && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=$(LIBFUZZER_SECONDS) $(OUT)/corpus; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

# R11: no float type anywhere in the C sources (the z-wasm kit's checker;
# comments and strings are excluded). The bindings are checked too.
check-nofloat:
	$(PYTHON) ../../kits/z-wasm/tools/check_nofloat.py $(SRC) src/kr_internal.h include/zkron.h \
	  tools/zkron_tool.c test/test_parse.c test/test_eval.c test/test_post.c test/fuzz_parse.c \
	  test/kr_test.h bindings/k/zkron_k.c bindings/apl/zkron_gnuapl.cc
	@echo "compiler guard for the library sources: $(or $(NOFLOAT),none on this compiler)"

# ---- Bindings ----
# ngn/k: `"libzkron_k.so"2:(`kron;1)` loads a C function; k.h comes from
# the ngn/k source tree. The extension resolves k's symbols from the k
# binary, so k must be linked with -Wl,-E (see the README).
NGNK     ?=
NGNK_SRC ?=
GNUAPL     ?=
GNUAPL_SRC ?=

$(OUT)/libzkron_k.$(SOEXT): bindings/k/zkron_k.c $(SRC) $(HDRS)
	@test -n "$(NGNK_SRC)" || { echo "set NGNK_SRC to the ngn/k source tree (for k.h)"; exit 1; }
	@mkdir -p $(OUT)
	$(CC) -std=c11 -O2 -g -fPIC $(WARN) $(SAN) -Iinclude -Isrc -I$(ZXV)/include -isystem $(NGNK_SRC) \
	  $(SHARED) bindings/k/zkron_k.c $(SRC) -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -o $@

bindings-k: $(OUT)/libzkron_k.$(SOEXT)
	@test -n "$(NGNK)" || { echo "set NGNK to an ngn/k binary"; exit 1; }
	cd bindings/k && $(abspath $(NGNK)) test_kron.k $(abspath $(OUT)/libzkron_k.$(SOEXT))

# GNU APL native function: 'libzkron_apl.so' ⎕FX 'KRON'
$(OUT)/libzkron_apl.$(SOEXT): bindings/apl/zkron_gnuapl.cc $(LIB) $(HDRS)
	@test -n "$(GNUAPL_SRC)" || { echo "set GNUAPL_SRC to the configured GNU APL source tree"; exit 1; }
	@mkdir -p $(OUT)
	$(CXX) -std=gnu++17 -O2 -g -fPIC -Wall -Wextra -Werror -Wno-unused-parameter $(SAN) \
	  -Iinclude -I$(ZXV)/include -isystem $(GNUAPL_SRC) -isystem $(GNUAPL_SRC)/src $(SHARED) \
	  bindings/apl/zkron_gnuapl.cc $(LIB) -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -o $@

bindings-apl: $(OUT)/libzkron_apl.$(SOEXT)
	@test -n "$(GNUAPL)" || { echo "set GNUAPL to a GNU APL binary"; exit 1; }
	sh bindings/apl/run_test.sh $(abspath $(GNUAPL)) $(abspath $(OUT)/libzkron_apl.$(SOEXT)) bindings/apl/test_kron.apl

bindings: bindings-k bindings-apl

clean:
	rm -rf $(OUT)
