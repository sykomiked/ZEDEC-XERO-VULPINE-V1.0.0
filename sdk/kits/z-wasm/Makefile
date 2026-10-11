# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/kits/z-wasm — Z-Wasm, the ZXV plugin sandbox kit.
#
#   make                 build/libzwasm.a and build/zw-tool
#   make test            unit tests, host tests, the example plugins, a fuzz
#                        smoke run and the no-float check
#   make examples        build/examples/*.wasm (needs zig: pip install ziglang)
#   make demo            run the host demo on the example plugins
#   make fuzz            the deterministic mutation fuzzer (FUZZ_ITERS=...)
#   make fuzz-libfuzzer  the libFuzzer target, if clang ships libFuzzer
#   make test-node       cross-check metered modules with Node's
#                        WebAssembly.validate (needs node)
#   make check-nofloat   R11: no float/double in the validator and meter
#   make verify-third-party  the vendored wasm3 matches THIRD_PARTY.md
#   make clean
#
# Options:
#   SANITIZE=1           -fsanitize=address,undefined for the kit, wasm3 and
#                        the tests (libzxv is linked as built)
#   WASMCC=...           the wasm32 C compiler (default: python3 -m ziglang cc)
#   ZW_SKIP_EXAMPLES=1   make test without zig (the example-plugin tests skip)
#
# libzxv is built with `make -C ../../libzxv` first (this Makefile does it).

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
WASMCC  ?= $(PYTHON) -m ziglang cc
OUT     ?= build
ZXV     := ../../libzxv
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
endif
ZXV_ABS := $(abspath $(ZXV)/build)

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif

# R11 guard, second line: -mgeneral-regs-only on the R11 sources where the
# compiler accepts it. On arm64 (gcc, clang) any float use is then a compile
# error; on x86-64 gcc only refuses float arguments and returns, and clang
# accepts it without enforcing. The authoritative check is check-nofloat.
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) \
           -Iinclude -Isrc -I$(ZXV)/include -Ithird_party/wasm3/source
# wasm3 is third-party: its own warnings are not ours to fix (-w), and its
# float support is compiled out (d_m3HasFloat=0).
W3FLAGS := -std=gnu99 -O2 -g -fPIC -w $(SAN) -Dd_m3HasFloat=0 -Dd_m3LogOutput=0 \
           -Ithird_party/wasm3/source

NOFLOAT_SRC := src/zw_wasm.c src/zw_policy.c src/zw_validate.c src/zw_meter.c
HOST_SRC    := src/zw_host.c
W3_SRC      := $(addprefix third_party/wasm3/source/,m3_bind.c m3_code.c m3_compile.c \
               m3_core.c m3_env.c m3_exec.c m3_function.c m3_info.c m3_module.c \
               m3_parse.c m3_validate.c)

NOFLOAT_OBJ := $(patsubst src/%.c,$(OUT)/obj/%.o,$(NOFLOAT_SRC))
HOST_OBJ    := $(patsubst src/%.c,$(OUT)/obj/%.o,$(HOST_SRC))
W3_OBJ      := $(patsubst third_party/wasm3/source/%.c,$(OUT)/w3/%.o,$(W3_SRC))
LIB         := $(OUT)/libzwasm.a
HDRS        := include/zwasm.h src/zw_wasm.h $(ZXV)/include/zxv_api.h

LINK := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -lm $(SAN)

EXAMPLES := $(OUT)/examples/rational_plugin.wasm $(OUT)/examples/float_plugin.wasm \
            $(OUT)/examples/spin_plugin.wasm
# wasm32 freestanding, no libc, integer-only codegen. --max-memory gives the
# memory a maximum (Z-Wasm refuses memory without one); 2 pages = 128 KiB.
WASMFLAGS := --target=wasm32-freestanding -O2 -nostdlib -mno-bulk-memory \
             -Wl,--no-entry -Wl,-z,stack-size=16384 \
             -Wl,--initial-memory=131072 -Wl,--max-memory=131072

FUZZ_ITERS ?= 20000

.PHONY: all test examples demo fuzz fuzz-libfuzzer test-node check-nofloat verify-third-party \
        clean libzxv

all: $(LIB) $(OUT)/zw-tool

libzxv:
	$(MAKE) -C $(ZXV) all

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) $(if $(filter $<,$(NOFLOAT_SRC)),$(NOFLOAT)) -c $< -o $@

$(OUT)/w3/%.o: third_party/wasm3/source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(W3FLAGS) -c $< -o $@

$(LIB): $(NOFLOAT_OBJ) $(HOST_OBJ) $(W3_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

TESTCC = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) -Iinclude -Isrc -I$(ZXV)/include -Itest

$(OUT)/zw-tool: tools/zw_tool.c $(LIB) $(ZXV_LIB) $(HDRS)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_validate: test/test_validate.c test/wasm_build.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_host: test/test_host.c test/wasm_build.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/fuzz_validate: test/fuzz_validate.c test/wasm_build.h $(LIB) $(ZXV_LIB)
	$(TESTCC) -DZW_FUZZ_MAIN $< $(LIB) $(LINK) -o $@

$(OUT)/host_demo: examples/host_demo.c $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/examples/%.wasm: examples/plugins/%.c examples/plugins/zxv_plugin.h
	@mkdir -p $(dir $@)
	$(WASMCC) $(WASMFLAGS) $< -o $@

examples: $(EXAMPLES)

ifeq ($(ZW_SKIP_EXAMPLES),)
TEST_EXAMPLES := $(EXAMPLES)
EXAMPLE_ARGS  := $(OUT)/examples/rational_plugin.wasm $(OUT)/examples/float_plugin.wasm \
                 $(OUT)/examples/spin_plugin.wasm
endif

test: check-nofloat verify-third-party $(OUT)/test_validate $(OUT)/test_host $(OUT)/fuzz_validate $(OUT)/zw-tool \
      $(TEST_EXAMPLES)
	$(OUT)/test_validate
	$(OUT)/test_host $(EXAMPLE_ARGS)
	$(OUT)/fuzz_validate 2000 $(EXAMPLE_ARGS)

demo: $(OUT)/host_demo $(EXAMPLES)
	$(OUT)/host_demo $(EXAMPLES)

fuzz: $(OUT)/fuzz_validate $(TEST_EXAMPLES)
	$(OUT)/fuzz_validate $(FUZZ_ITERS) $(EXAMPLE_ARGS)

# libFuzzer: clang with the compiler-rt fuzzer runtime (not every distro
# ships it). Runs 60 s from the seed corpus written by fuzz_validate.
fuzz-libfuzzer: $(LIB) $(TEST_EXAMPLES)
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  mkdir -p $(OUT)/corpus && \
	  $(MAKE) --no-print-directory $(OUT)/fuzz_validate && \
	  ZW_FUZZ_CORPUS=$(OUT)/corpus $(OUT)/fuzz_validate 0 $(EXAMPLE_ARGS) && \
	  clang -std=gnu11 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude -Isrc \
	    -I$(ZXV)/include -Itest -Ithird_party/wasm3/source test/fuzz_validate.c \
	    $(NOFLOAT_SRC) $(HOST_SRC) $(W3_SRC) -Dd_m3HasFloat=0 \
	    -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -lm -o $(OUT)/fuzz_libfuzzer && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=60 $(OUT)/corpus; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

test-node: $(OUT)/zw-tool $(OUT)/fuzz_validate $(TEST_EXAMPLES)
	@mkdir -p $(OUT)/corpus
	ZW_FUZZ_CORPUS=$(OUT)/corpus $(OUT)/fuzz_validate 0 $(EXAMPLE_ARGS)
	node tools/node_crosscheck.mjs $(OUT)/zw-tool $(EXAMPLE_ARGS) $(OUT)/corpus/*.wasm

# R11 for the validator and meter: no float types, no math.h. (wasm3's
# float code is compiled out; zw_host.c only sees wasm3's header union.)
check-nofloat:
	$(PYTHON) tools/check_nofloat.py $(NOFLOAT_SRC) src/zw_wasm.h include/zwasm.h
	@echo "compiler guard for the R11 sources: $(or $(NOFLOAT),none on this compiler)"

verify-third-party:
	$(PYTHON) tools/verify_third_party.py

clean:
	rm -rf $(OUT)
