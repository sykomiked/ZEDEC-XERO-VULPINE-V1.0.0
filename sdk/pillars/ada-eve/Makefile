# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/pillars/ada-eve — the Ada binding (package Zxv) and Eve, the compact
# shorthand for Ada type declarations, over libzxv.
#
#   make                 build/libzeve.a, build/zeve (the command-line tool)
#                        and, with GNAT, the Ada binding's objects
#   make test            C unit tests, golden scripts, a fuzz smoke run, the
#                        no-float checks, and with GNAT the Ada conformance
#                        and layout tests, the fail-closed test, the
#                        compile-fail tests and the GNAT golden comparison
#   make test-c          only the C half (no Ada compiler needed)
#   make test-ada        only the Ada half
#   make golden          the golden scripts (test/golden/*.golden)
#   make golden-gnat     GNAT compiles every layout and agrees with libzeve
#                        on every type's bounds, digits, literals and sizes
#   make fatal           a poisoned context stops an Ada program with exit 70
#   make compile-fail    what the Ada types must refuse to compile
#   make prove           gnatprove on Zxv.Fixed (the SPARK part), if installed
#   make demo            examples/eve_demo.adb (or the tool, without GNAT)
#   make fuzz            the deterministic parser/codec fuzzer (FUZZ_ITERS=...)
#   make fuzz-libfuzzer  the libFuzzer target, if clang ships libFuzzer
#   make check-nofloat   R11: no float type in the C sources
#   make check-nofloat-ada  R11: no Float, real literal or fixed-point type in
#                        the Ada sources outside the one marked boundary
#   make clean
#
# Options:
#   SANITIZE=1           -fsanitize=address,undefined (C; address for Ada)
#   ADA=0|1              skip or require the Ada half (default: 1 if gnatmake exists)
#   GNATPROVE=gnatprove  the prover for `make prove`
#   OUT=dir              build directory (default build/, git-ignored)
#
# libzxv is built with `make -C ../../libzxv all`.

ifeq ($(origin CC),default)
  CC := gcc
endif
AR        ?= ar
PYTHON    ?= python3
GNATMAKE  ?= gnatmake
GNATCC    ?= gcc
GNATPROVE ?= gnatprove
OUT       ?= build
ZXV       := ../../libzxv
UNAME_S   := $(shell uname -s)
ADA       ?= $(if $(shell command -v $(GNATMAKE) 2>/dev/null),1,0)

ifeq ($(UNAME_S),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
endif
ZXV_ABS := $(abspath $(ZXV)/build)

ifneq ($(SANITIZE),)
  SAN    := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
  ADASAN := -fsanitize=address -fno-omit-frame-pointer
endif

# R11 compiler guard where the compiler enforces it (x86-64 and arm64 gcc/clang).
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) \
           -Iinclude -Isrc -I$(ZXV)/include

SRC  := src/ze_util.c src/ze_lex.c src/ze_pkg.c src/ze_ada.c src/ze_eve.c src/ze_print.c \
        src/ze_codec.c src/ze_post.c
OBJ  := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRC))
LIB  := $(OUT)/libzeve.a
HDRS := include/zeve.h src/ze_internal.h $(ZXV)/include/zxv_api.h

LINK   := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread $(SAN)
TESTCC  = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) -Iinclude -Isrc -I$(ZXV)/include -Itest

# ---- Ada ----
ADAFLAGS := -gnat2012 -gnata -gnatwa -gnatwe -gnatVa -gnatf -g -O1
ADASRC   := $(wildcard ada/*.ads ada/*.adb)
AOBJ     := $(OUT)/aobj
GEN      := $(OUT)/gen/conformance_vectors.ads
# gnatmake runs inside $(AOBJ), so its objects and the binder's files stay there
ADAMAKE   = cd $(AOBJ) && $(GNATMAKE) -q $(ADAFLAGS) -aI$(abspath ada) -aI$(abspath test) -aI$(abspath $(OUT)/gen)
ADALINK   = -cargs $(ADASAN) -largs $(abspath $(LIB)) $(LINK) $(ADASAN)

CTESTS   := test_ada test_eve test_codec test_post
ADATESTS := test_zxv test_eve_ada
GOLDEN_LAYOUTS := examples/payments.ads examples/payments.eve \
                  $(filter-out test/golden/refuse_%,$(wildcard test/golden/*.ads test/golden/*.eve))
FUZZ_ITERS ?= 20000
LIBFUZZER_SECONDS ?= 60

.PHONY: all test test-c test-ada golden golden-gnat fatal compile-fail prove demo fuzz \
        fuzz-libfuzzer check-nofloat check-nofloat-ada clean ada-required ada-binding

ifeq ($(ADA),1)
all: $(LIB) $(OUT)/zeve ada-binding
test: test-c test-ada
	@echo "ada-eve: all tests passed (C and Ada)"
else
all: $(LIB) $(OUT)/zeve
test: test-c
	@echo "ada-eve: C tests passed; Ada SKIPPED (ADA=0, or no $(GNATMAKE); ADA=1 to require it)"
endif

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/zeve: tools/zeve_tool.c $(LIB) $(ZXV_LIB) $(HDRS)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_%: test/test_%.c test/ze_test.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/fuzz_parse: test/fuzz_parse.c $(LIB) $(ZXV_LIB)
	$(TESTCC) -DZE_FUZZ_MAIN $< $(LIB) $(LINK) -o $@

golden: $(OUT)/zeve
	$(OUT)/zeve golden test/golden/*.golden

test-c: check-nofloat $(addprefix $(OUT)/,$(CTESTS)) $(OUT)/fuzz_parse $(OUT)/zeve
	@for t in $(CTESTS); do $(OUT)/$$t || exit 1; done
	$(OUT)/zeve golden test/golden/*.golden
	$(OUT)/fuzz_parse 3000 examples/*.ads examples/*.eve test/golden/*.ads test/golden/*.eve
	$(OUT)/zeve check examples/payments.ads examples/payments.eve
	$(OUT)/zeve same examples/payments.ads examples/payments.eve
	@if $(OUT)/zeve check examples/broken.ads 2>/dev/null; then echo "broken.ads parsed"; exit 1; fi
	@if $(OUT)/zeve check examples/broken.eve 2>/dev/null; then echo "broken.eve parsed"; exit 1; fi
	@echo "ada-eve: C tests passed"

# ---- Ada half ----
ada-required:
	@command -v $(GNATMAKE) >/dev/null 2>&1 || { echo "$(GNATMAKE) not found: install GNAT or use ADA=0"; exit 1; }

# vectors.json as an Ada package of constants
$(GEN): ../../conformance/vectors.json tools/gen_vectors_ada.py
	@mkdir -p $(OUT)/gen
	$(PYTHON) tools/gen_vectors_ada.py $< $@

# Compile every unit once, so the test programs below never compile a
# shared unit at the same time (make -j).
$(AOBJ)/.binding: $(ADASRC) test/test_support.ads test/test_support.adb $(GEN) | ada-required
	@mkdir -p $(AOBJ)
	$(ADAMAKE) -c $(abspath $(filter %.adb,$(ADASRC)) test/test_support.adb $(GEN)) -cargs $(ADASAN)
	@touch $@

ada-binding: $(AOBJ)/.binding

$(OUT)/test_zxv: test/test_zxv.adb $(AOBJ)/.binding $(LIB) $(ZXV_LIB)
	$(ADAMAKE) $(abspath $<) -o $(abspath $@) $(ADALINK)

$(OUT)/test_eve_ada: test/test_eve_ada.adb $(AOBJ)/.binding $(LIB) $(ZXV_LIB)
	$(ADAMAKE) $(abspath $<) -o $(abspath $@) $(ADALINK)

$(OUT)/test_fatal: test/test_fatal.adb $(AOBJ)/.binding $(LIB) $(ZXV_LIB)
	$(ADAMAKE) $(abspath $<) -o $(abspath $@) $(ADALINK)

$(OUT)/eve_demo: examples/eve_demo.adb $(AOBJ)/.binding $(LIB) $(ZXV_LIB)
	$(ADAMAKE) $(abspath $<) -o $(abspath $@) $(ADALINK)

# A poisoned context must stop the program: exit 70, the reason on stderr,
# and nothing after the stop runs, even inside "when others" and through
# the status forms (Ledger.Try_Post and Eve.Try_Post).
fatal: $(OUT)/test_fatal
	@for mode in ledger eve; do \
	  set +e; $(OUT)/test_fatal $$( [ $$mode = eve ] && echo eve ) > $(OUT)/fatal.out 2> $(OUT)/fatal.err; rc=$$?; set -e; \
	  cat $(OUT)/fatal.out $(OUT)/fatal.err; \
	  test $$rc -eq 70 || { echo "test_fatal ($$mode): exit $$rc, expected 70"; exit 1; }; \
	  grep -q 'ZXV_E_POISONED' $(OUT)/fatal.err || { echo "test_fatal ($$mode): no ZXV_E_POISONED on stderr"; exit 1; }; \
	  if grep -q 'AFTER-STOP' $(OUT)/fatal.out; then echo "test_fatal ($$mode): code ran after the stop"; exit 1; fi; \
	  echo "test_fatal ($$mode): stopped with 70 on ZXV_E_POISONED"; \
	done

compile-fail: | ada-required
	GNATCC="$(GNATCC)" ADAINC="$(abspath ada)" sh test/compile_fail/run.sh $(abspath test/compile_fail)

golden-gnat: $(OUT)/zeve | ada-required
	sh tools/golden_gnat.sh $(abspath $(OUT)/zeve) $(OUT) $(GOLDEN_LAYOUTS)

test-ada: check-nofloat-ada $(addprefix $(OUT)/,$(ADATESTS)) fatal compile-fail golden-gnat
	@for t in $(ADATESTS); do $(OUT)/$$t || exit 1; done
	@echo "ada-eve: Ada tests passed"

prove:
	@if command -v $(GNATPROVE) >/dev/null 2>&1; then \
	  $(GNATPROVE) -P ada/prove.gpr -u zxv-fixed.adb -j0; \
	else echo "gnatprove not found: Zxv.Fixed is not proved on this machine (see README.md, SPARK)"; fi

ifeq ($(ADA),1)
demo: $(OUT)/eve_demo
	$(OUT)/eve_demo
else
demo: $(OUT)/zeve
	$(OUT)/zeve same examples/payments.ads examples/payments.eve
	$(OUT)/zeve canon examples/payments.ads
	$(OUT)/zeve decode examples/payments.eve payment \
	  494e562d323032342d30303031202020000000000001e2400000000001ffffffff0134d703ffffff0600000000000186a000000000000000000000000000000000000000000000004b01
endif

fuzz: $(OUT)/fuzz_parse
	$(OUT)/fuzz_parse $(FUZZ_ITERS) examples/*.ads examples/*.eve test/golden/*.ads test/golden/*.eve

fuzz-libfuzzer:
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  mkdir -p $(OUT)/corpus && cp examples/*.ads examples/*.eve test/golden/*.ads test/golden/*.eve $(OUT)/corpus/ && \
	  clang -std=gnu11 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude -Isrc \
	    -I$(ZXV)/include test/fuzz_parse.c $(SRC) \
	    -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -o $(OUT)/fuzz_libfuzzer && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=$(LIBFUZZER_SECONDS) $(OUT)/corpus; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

# R11: no float type anywhere in the C sources (the z-wasm kit's checker;
# comments and strings are excluded).
check-nofloat:
	$(PYTHON) ../../kits/z-wasm/tools/check_nofloat.py $(SRC) src/ze_internal.h include/zeve.h \
	  tools/zeve_tool.c $(addprefix test/,$(addsuffix .c,$(CTESTS))) test/fuzz_parse.c test/ze_test.h
	@echo "compiler guard for the library sources: $(or $(NOFLOAT),none on this compiler)"

# R11 for Ada: the binding, the tests and the demo (the compile-fail cases
# exist to contain floats and are not checked).
check-nofloat-ada: $(GEN)
	$(PYTHON) tools/check_nofloat_ada.py $(ADASRC) test/*.ads test/*.adb examples/*.adb $(GEN)

clean:
	rm -rf $(OUT)
