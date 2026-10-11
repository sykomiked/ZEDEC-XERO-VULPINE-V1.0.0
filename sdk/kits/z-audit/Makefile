# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/kits/z-audit — a static checker for ZXV plugins, run in CI before a
# plugin is published.
#
#   make                 build/libzaudit.a and build/z-audit (the CLI)
#   make test            unit tests, the CLI on the example plugins, a fuzz smoke
#                        run and the no-float check
#   make demo            z-audit on three example plugins (one passes, two fail)
#   make fuzz            the deterministic fuzzer (FUZZ_ITERS=...)
#   make fuzz-libfuzzer  the libFuzzer target, if clang ships libFuzzer
#   make check-nofloat   R11: no float type in the kit's sources
#   make clean
#
# Options:
#   SANITIZE=1            -fsanitize=address,undefined for the kit, z-wasm and
#                         z-contract (rebuilt under $(OUT)) and the tests
#   ZAU_SKIP_EXAMPLES=1   make test without zig (the example-plugin runs skip)
#
# z-wasm and z-contract are built into $(OUT)/zwasm and $(OUT)/zcontract; their
# own build/ directories are not touched.

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
WASMCC  ?= $(PYTHON) -m ziglang cc
OUT     ?= build
ZXV     := ../../libzxv
ZW      := ../z-wasm
ZC      := ../z-contract

ifeq ($(shell uname -s),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
endif
ZXV_ABS := $(abspath $(ZXV)/build)
OUT_ABS := $(abspath $(OUT))
ZW_OUT  := $(OUT_ABS)/zwasm
ZW_LIB  := $(ZW_OUT)/libzwasm.a
ZC_OUT  := $(OUT_ABS)/zcontract
ZC_LIB  := $(ZC_OUT)/libzcontract.a

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
INC     := -Iinclude -Isrc -I$(ZXV)/include -I$(ZW)/include -I$(ZW)/src -I$(ZC)/include
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) $(INC)

SRC  := src/zau_util.c src/zau_module.c src/zau_bounds.c src/zau_absint.c src/zau_audit.c \
        src/zau_report.c
OBJ  := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRC))
LIB  := $(OUT)/libzaudit.a
HDRS := include/zaudit.h src/zau_internal.h $(ZXV)/include/zxv_api.h $(ZW)/include/zwasm.h \
        $(ZW)/src/zw_wasm.h $(ZC)/include/zcontract.h

LIBS   = $(LIB) $(ZC_LIB) $(ZW_LIB)
LINK  := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -lm $(SAN)
TESTCC = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) $(INC) -I$(ZW)/test -Itest

# The example plugins (wasm32, built with zig like z-wasm's examples).
ZW_EX   := $(ZW_OUT)/examples
EX_WASM := $(OUT)/examples/audit_plugin.wasm
WASMFLAGS := --target=wasm32-freestanding -O2 -nostdlib -mno-bulk-memory \
             -Wl,--no-entry -Wl,-z,stack-size=16384 \
             -Wl,--initial-memory=131072 -Wl,--max-memory=131072
ifeq ($(ZAU_SKIP_EXAMPLES),)
TEST_EXAMPLES := $(ZW_EX)/rational_plugin.wasm $(ZW_EX)/float_plugin.wasm $(EX_WASM)
endif

FUZZ_ITERS ?= 20000

.PHONY: all test demo fuzz fuzz-libfuzzer check-nofloat clean run-examples

all: $(LIB) $(OUT)/z-audit

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(ZW_LIB): $(wildcard $(ZW)/src/*.c $(ZW)/src/*.h $(ZW)/include/*.h)
	$(MAKE) -C $(ZW) CC=$(CC) SANITIZE=$(SANITIZE) OUT=$(ZW_OUT) $(ZW_LIB)

$(ZC_LIB): $(wildcard $(ZC)/src/*.c $(ZC)/src/*.h $(ZC)/include/*.h)
	$(MAKE) -C $(ZC) CC=$(CC) SANITIZE=$(SANITIZE) OUT=$(ZC_OUT) $(ZC_LIB)

$(ZW_EX)/%.wasm: $(ZW)/examples/plugins/%.c
	$(MAKE) -C $(ZW) PYTHON=$(PYTHON) OUT=$(ZW_OUT) $@

$(OUT)/examples/%.wasm: examples/plugins/%.c $(ZW)/examples/plugins/zxv_plugin.h
	@mkdir -p $(dir $@)
	$(WASMCC) $(WASMFLAGS) -I$(ZW)/examples/plugins $< -o $@

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/z-audit: tools/zau_tool.c $(LIBS) $(ZXV_LIB) $(HDRS)
	$(TESTCC) $< $(LIBS) $(LINK) -o $@

$(OUT)/test_audit: test/test_audit.c test/zau_test.h $(LIBS) $(ZXV_LIB)
	$(TESTCC) $< $(LIBS) $(LINK) -o $@

$(OUT)/fuzz_audit: test/fuzz_audit.c test/zau_test.h $(LIBS) $(ZXV_LIB)
	$(TESTCC) -DZAU_FUZZ_MAIN $< $(LIBS) $(LINK) -o $@

# The CLI on the example plugins, with the exit status each must give.
run-examples: $(OUT)/z-audit $(TEST_EXAMPLES)
ifeq ($(ZAU_SKIP_EXAMPLES),)
	$(OUT)/z-audit --quiet --grant all --json $(OUT)/rational.json \
	  --contract examples/rational.zc $(ZW_EX)/rational_plugin.wasm
	$(PYTHON) -c "import json,sys; d=json.load(open(sys.argv[1])); sys.exit(d['result']!='pass')" \
	  $(OUT)/rational.json
	@$(OUT)/z-audit --quiet --grant all --contract examples/rational.zc $(ZW_EX)/float_plugin.wasm; \
	  test $$? -eq 1 || { echo "float_plugin.wasm should fail with exit 1"; exit 1; }
	@$(OUT)/z-audit --quiet --grant all --contract examples/partial.zc $(ZW_EX)/rational_plugin.wasm; \
	  test $$? -eq 1 || { echo "partial.zc should fail with exit 1"; exit 1; }
	@$(OUT)/z-audit --quiet --grant all --json $(OUT)/audit_plugin.json \
	  --contract examples/audit_plugin.zc $(EX_WASM); \
	  test $$? -eq 1 || { echo "audit_plugin.wasm should fail with exit 1"; exit 1; }
	$(PYTHON) -c "import json,sys; d=json.load(open(sys.argv[1])); c={f['code'] for f in d['findings']}; \
	  need={'RESULT_IN_RANGE','RESULT_MAY_LEAVE','PROPOSAL_OUT_OF_BOUNDS','NOT_ANALYZED'}; \
	  sys.exit(0 if need <= c else 'missing: %s' % (need - c))" $(OUT)/audit_plugin.json
	@$(OUT)/z-audit --grant all 2>/dev/null; test $$? -eq 2 || { echo "usage should exit 2"; exit 1; }
	@echo "run-examples: exit codes and JSON reports as expected"
else
	@echo "run-examples: skipped (ZAU_SKIP_EXAMPLES)"
endif

test: check-nofloat $(OUT)/test_audit $(OUT)/fuzz_audit $(OUT)/z-audit $(TEST_EXAMPLES)
	$(OUT)/test_audit
	$(OUT)/fuzz_audit 2000 $(TEST_EXAMPLES)
	$(MAKE) --no-print-directory run-examples

demo: $(OUT)/z-audit $(ZW_EX)/rational_plugin.wasm $(ZW_EX)/float_plugin.wasm $(EX_WASM)
	-$(OUT)/z-audit --grant all --contract examples/rational.zc $(ZW_EX)/rational_plugin.wasm
	-$(OUT)/z-audit --grant all --contract examples/rational.zc $(ZW_EX)/float_plugin.wasm
	-$(OUT)/z-audit --grant all --contract examples/audit_plugin.zc $(EX_WASM)

fuzz: $(OUT)/fuzz_audit $(TEST_EXAMPLES)
	$(OUT)/fuzz_audit $(FUZZ_ITERS) $(TEST_EXAMPLES)

fuzz-libfuzzer: $(ZC_LIB) $(ZW_LIB)
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  mkdir -p $(OUT)/corpus && \
	  clang -std=gnu11 -O1 -g -fsanitize=fuzzer,address,undefined $(INC) -I$(ZW)/test -Itest \
	    -I$(ZC)/src test/fuzz_audit.c $(SRC) \
	    $(ZW)/src/zw_wasm.c $(ZW)/src/zw_policy.c $(ZW)/src/zw_validate.c \
	    $(ZC)/src/zc_util.c $(ZC)/src/zc_parse.c $(ZC)/src/zc_check.c $(ZC)/src/zc_evlog.c \
	    $(ZC)/src/zc_hexsink.c \
	    -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -lm -o $(OUT)/fuzz_libfuzzer && \
	  ZAU_FUZZ_CORPUS=$(OUT)/corpus $(MAKE) --no-print-directory $(OUT)/fuzz_audit && \
	  ZAU_FUZZ_CORPUS=$(OUT)/corpus $(OUT)/fuzz_audit 0 && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=60 $(OUT)/corpus; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

check-nofloat:
	$(PYTHON) $(ZW)/tools/check_nofloat.py $(SRC) src/zau_internal.h include/zaudit.h tools/zau_tool.c \
	  test/*.c test/*.h
	@echo "compiler guard for the kit sources: $(or $(NOFLOAT),none on this compiler)"

clean:
	rm -rf $(OUT)
