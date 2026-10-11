# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/kits/z-ffi — typed, capability-gated wrappers for calling C libraries.
#
#   make                  build/libzffi.a and build/zf-gen
#   make test             runtime, generator and wrapper tests, the signature
#                         checks (specs that must NOT compile), a fuzz smoke run,
#                         the demo and the no-float check
#   make demo             call libc and libm through the generated wrappers
#   make fuzz             the deterministic spec fuzzer (FUZZ_ITERS=...)
#   make fuzz-libfuzzer   the libFuzzer target, if clang ships libFuzzer
#   make check-signatures compile the bad specs in test/ and expect refusal
#   make check-nofloat    R11: no float type in the runtime, generator and tests
#   make clean
#
# Options:
#   SANITIZE=1            -fsanitize=address,undefined everywhere
#
# Generated wrappers go to $(OUT)/gen. They are compiled with the same
# warnings but without the no-float guard: they hold float values for the
# length of a call (see README.md, "R11 and generated code").

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../../libzxv
ZW      := ../z-wasm

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

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) \
           -Iinclude -Isrc -I$(ZXV)/include
GENFLAGS = -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) -Iinclude -I$(ZXV)/include

SRC  := src/zf_runtime.c src/zf_gen.c
OBJ  := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRC))
LIB  := $(OUT)/libzffi.a
HDRS := include/zffi.h src/zf_internal.h $(ZXV)/include/zxv_api.h

GEN     := $(OUT)/gen
SPECS   := libc libm
GEN_OBJ := $(patsubst %,$(GEN)/%_zf.o,$(SPECS))

LINK   := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -lm $(SAN)
TESTCC  = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) -Iinclude -Isrc -I$(ZXV)/include -Itest -I$(GEN)

FUZZ_ITERS ?= 20000

.PHONY: all test demo fuzz fuzz-libfuzzer check-signatures check-nofloat clean

# keep the generated wrappers (make would delete them as intermediates)
.SECONDARY:

all: $(LIB) $(OUT)/zf-gen

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/zf-gen: tools/zf_gen_tool.c $(LIB) $(ZXV_LIB) $(HDRS)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

# spec -> wrappers -> object
$(GEN)/%_zf.c $(GEN)/%_zf.h: examples/%.zf $(OUT)/zf-gen
	@mkdir -p $(GEN)
	$(OUT)/zf-gen $< $(GEN)/$*_zf.h $(GEN)/$*_zf.c

$(GEN)/%_zf.o: $(GEN)/%_zf.c $(GEN)/%_zf.h include/zffi.h
	$(CC) $(GENFLAGS) -I$(GEN) -c $< -o $@

$(OUT)/test_runtime: test/test_runtime.c test/zf_test.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_gen: test/test_gen.c test/zf_test.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_wrappers: test/test_wrappers.c test/zf_test.h $(GEN_OBJ) $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(GEN_OBJ) $(LIB) $(LINK) -o $@

$(OUT)/fuzz_gen: test/fuzz_gen.c $(LIB) $(ZXV_LIB)
	$(TESTCC) -DZF_FUZZ_MAIN $< $(LIB) $(LINK) -o $@

$(OUT)/ffi_demo: examples/ffi_demo.c $(GEN_OBJ) $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(GEN_OBJ) $(LIB) $(LINK) -o $@

test: check-nofloat $(OUT)/test_runtime $(OUT)/test_gen $(OUT)/test_wrappers $(OUT)/fuzz_gen $(OUT)/ffi_demo check-signatures
	$(OUT)/test_runtime
	$(OUT)/test_gen
	$(OUT)/test_wrappers
	$(OUT)/fuzz_gen 3000
	$(OUT)/ffi_demo > /dev/null

demo: $(OUT)/ffi_demo
	$(OUT)/ffi_demo

fuzz: $(OUT)/fuzz_gen
	$(OUT)/fuzz_gen $(FUZZ_ITERS)

fuzz-libfuzzer:
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  mkdir -p $(OUT)/corpus && cp examples/*.zf $(OUT)/corpus/ && \
	  clang -std=gnu11 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude -Isrc -I$(ZXV)/include \
	    test/fuzz_gen.c $(SRC) -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -o $(OUT)/fuzz_libfuzzer && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=60 $(OUT)/corpus; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

# Each spec in test/bad_*.zf disagrees with its C header (or with its C
# type's range). zf-gen accepts it (it cannot see the header); the C
# compiler must refuse the generated code with z-ffi's own static assertion.
check-signatures: $(OUT)/zf-gen
	@mkdir -p $(OUT)/bad
	@for t in bad_signature bad_float bad_range; do \
	  $(OUT)/zf-gen test/$$t.zf $(OUT)/bad/$${t}_zf.h $(OUT)/bad/$${t}_zf.c || exit 1; \
	  if $(CC) $(GENFLAGS) -I$(OUT)/bad -c $(OUT)/bad/$${t}_zf.c -o $(OUT)/bad/$$t.o 2> $(OUT)/bad/$$t.log; then \
	    echo "check-signatures: test/$$t.zf COMPILED, but it must not"; exit 1; fi; \
	  if ! grep -E -q "not how the included headers declare it|must fit" $(OUT)/bad/$$t.log; then \
	    cat $(OUT)/bad/$$t.log; echo "check-signatures: test/$$t.zf failed for another reason"; exit 1; fi; \
	  echo "check-signatures: test/$$t.zf refused by the compiler, as it must be"; \
	done

check-nofloat:
	$(PYTHON) $(ZW)/tools/check_nofloat.py $(SRC) src/zf_internal.h include/zffi.h tools/zf_gen_tool.c \
	  test/test_runtime.c test/test_gen.c test/test_wrappers.c test/fuzz_gen.c test/zf_test.h examples/ffi_demo.c
	@echo "compiler guard for the runtime and generator: $(or $(NOFLOAT),none on this compiler)"
	@echo "generated wrappers in $(GEN) are outside this check (they hold the C call's float values)"

clean:
	rm -rf $(OUT)
