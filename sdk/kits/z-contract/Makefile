# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/kits/z-contract — typed contracts with exact bounds for ZXV plugins.
#
#   make                 build/libzcontract.a, build/libzcontract_wasm.a, build/zc-tool
#   make test            parser, checker, evidence-log, quarantine-sink and Z-Wasm tests, a fuzz
#                        smoke run and the no-float check
#   make demo            the contract demo on the z-wasm rational example plugin
#   make fuzz            the deterministic parser fuzzer (FUZZ_ITERS=...)
#   make fuzz-libfuzzer  the libFuzzer target, if clang ships libFuzzer
#   make check-nofloat   R11: no float type in the kit's sources
#   make clean
#
# Options:
#   SANITIZE=1           -fsanitize=address,undefined for the kit, z-wasm (rebuilt
#                        under $(OUT)/zwasm) and the tests
#   ZC_SKIP_EXAMPLES=1   make test without zig (the example-plugin tests skip)
#
# libzxv is built with `make -C ../../libzxv` and z-wasm into $(OUT)/zwasm
# (its own build/ directory is not touched).

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../../libzxv
ZW      := ../z-wasm
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
endif
ZXV_ABS := $(abspath $(ZXV)/build)
OUT_ABS := $(abspath $(OUT))
ZW_OUT  := $(OUT_ABS)/zwasm
ZW_LIB  := $(ZW_OUT)/libzwasm.a

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif

# R11 compiler guard where the compiler enforces it (see z-wasm's Makefile).
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) \
           -Iinclude -Isrc -I$(ZXV)/include -I$(ZW)/include

CORE_SRC := src/zc_util.c src/zc_parse.c src/zc_check.c src/zc_evlog.c src/zc_hexsink.c
WASM_SRC := src/zc_wasm.c
CORE_OBJ := $(patsubst src/%.c,$(OUT)/obj/%.o,$(CORE_SRC))
WASM_OBJ := $(patsubst src/%.c,$(OUT)/obj/%.o,$(WASM_SRC))
LIB      := $(OUT)/libzcontract.a
WLIB     := $(OUT)/libzcontract_wasm.a
HDRS     := include/zcontract.h include/zcontract_wasm.h src/zc_internal.h \
            $(ZXV)/include/zxv_api.h $(ZW)/include/zwasm.h

LINK  := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -lm $(SAN)
TESTCC = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) -Iinclude -Isrc -I$(ZXV)/include \
         -I$(ZW)/include -I$(ZW)/test -Itest

EXAMPLE_WASM := $(ZW_OUT)/examples/rational_plugin.wasm
ifeq ($(ZC_SKIP_EXAMPLES),)
TEST_EXAMPLES := $(EXAMPLE_WASM)
EXAMPLE_ARGS  := $(EXAMPLE_WASM)
endif

FUZZ_ITERS ?= 20000

.PHONY: all test demo fuzz fuzz-libfuzzer check-nofloat clean zwasm

all: $(LIB) $(WLIB) $(OUT)/zc-tool

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

# z-wasm, built into our tree with our CC and sanitizer flags
$(ZW_LIB): $(wildcard $(ZW)/src/*.c $(ZW)/src/*.h $(ZW)/include/*.h)
	$(MAKE) -C $(ZW) CC=$(CC) SANITIZE=$(SANITIZE) OUT=$(ZW_OUT) $(ZW_LIB)

$(EXAMPLE_WASM): $(ZW)/examples/plugins/rational_plugin.c
	$(MAKE) -C $(ZW) PYTHON=$(PYTHON) OUT=$(ZW_OUT) $(EXAMPLE_WASM)

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(CORE_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(WLIB): $(WASM_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/zc-tool: tools/zc_tool.c $(LIB) $(ZXV_LIB) $(HDRS)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_parse: test/test_parse.c test/zc_test.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_check: test/test_check.c test/zc_test.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_hexsink: test/test_hexsink.c test/zc_test.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_wasm: test/test_wasm.c test/zc_test.h $(WLIB) $(LIB) $(ZW_LIB) $(ZXV_LIB)
	$(TESTCC) $< $(WLIB) $(LIB) $(ZW_LIB) $(LINK) -o $@

$(OUT)/fuzz_parse: test/fuzz_parse.c $(LIB) $(ZXV_LIB)
	$(TESTCC) -DZC_FUZZ_MAIN $< $(LIB) $(LINK) -o $@

$(OUT)/contract_demo: examples/contract_demo.c $(WLIB) $(LIB) $(ZW_LIB) $(ZXV_LIB)
	$(TESTCC) $< $(WLIB) $(LIB) $(ZW_LIB) $(LINK) -o $@

test: check-nofloat $(OUT)/test_parse $(OUT)/test_check $(OUT)/test_hexsink $(OUT)/test_wasm $(OUT)/fuzz_parse \
      $(OUT)/zc-tool $(TEST_EXAMPLES)
	$(OUT)/test_parse
	$(OUT)/test_check
	$(OUT)/test_hexsink
	$(OUT)/test_wasm $(EXAMPLE_ARGS)
	$(OUT)/fuzz_parse 3000 examples/*.zc
	$(OUT)/zc-tool check examples/fees.zc
	@if $(OUT)/zc-tool check examples/broken.zc; then echo "broken.zc parsed"; exit 1; fi

demo: $(OUT)/contract_demo $(EXAMPLE_WASM)
	$(OUT)/contract_demo examples/fees.zc $(EXAMPLE_WASM)

fuzz: $(OUT)/fuzz_parse
	$(OUT)/fuzz_parse $(FUZZ_ITERS) examples/*.zc

fuzz-libfuzzer: $(OUT)/fuzz_parse
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  mkdir -p $(OUT)/corpus && cp examples/*.zc $(OUT)/corpus/ && \
	  clang -std=gnu11 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude -Isrc \
	    -I$(ZXV)/include test/fuzz_parse.c $(CORE_SRC) \
	    -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -lm -o $(OUT)/fuzz_libfuzzer && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=60 $(OUT)/corpus; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

# R11: no float type anywhere in the kit (z-wasm's checker; comments and
# strings are excluded).
check-nofloat:
	$(PYTHON) $(ZW)/tools/check_nofloat.py $(CORE_SRC) $(WASM_SRC) src/zc_internal.h \
	  include/zcontract.h include/zcontract_wasm.h tools/zc_tool.c
	@echo "compiler guard for the kit sources: $(or $(NOFLOAT),none on this compiler)"

clean:
	rm -rf $(OUT)
