# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/pillars/cobol-sutra — COBOL copybooks and Sutra over libzxv.
#
#   make                 build/libzsutra.a and build/zsutra (the command-line tool)
#   make test            unit tests, golden records, equivalence, posting bridge,
#                        a fuzz smoke run and the no-float check
#   make demo            the payment-posting demo
#   make fuzz            the deterministic parser fuzzer (FUZZ_ITERS=...)
#   make fuzz-libfuzzer  the libFuzzer target, if clang ships libFuzzer
#   make cobol           GnuCOBOL interop: build and run cobol/PAYPOST.cbl and
#                        check its output (needs cobc)
#   make golden-cobol    regenerate the golden records with GnuCOBOL and compare
#                        them with test/golden (needs cobc)
#   make check-nofloat   R11: no float type in the pillar's sources
#   make clean
#
# Options:
#   SANITIZE=1           -fsanitize=address,undefined for the pillar and its tests
#   OUT=dir              build directory (default build/, git-ignored)
#
# libzxv is built with `make -C ../../libzxv all`.

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
COBC    ?= cobc
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

# R11 compiler guard where the compiler enforces it (x86-64 and arm64 gcc/clang).
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) \
           -Iinclude -Isrc -I$(ZXV)/include

SRC  := src/zs_util.c src/zs_picture.c src/zs_layout.c src/zs_copybook.c src/zs_sutra.c \
        src/zs_codec.c src/zs_post.c
OBJ  := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRC))
LIB  := $(OUT)/libzsutra.a
HDRS := include/zsutra.h src/zs_internal.h $(ZXV)/include/zxv_api.h

LINK  := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread $(SAN)
TESTCC = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) -Iinclude -Isrc -I$(ZXV)/include -Itest

TESTS := test_copybook test_sutra test_codec test_golden test_post
FUZZ_ITERS ?= 20000
LIBFUZZER_SECONDS ?= 60

.PHONY: all test demo fuzz fuzz-libfuzzer cobol golden-cobol check-nofloat clean

all: $(LIB) $(OUT)/zsutra

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/zsutra: tools/zsutra_tool.c $(LIB) $(ZXV_LIB) $(HDRS)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_%: test/test_%.c test/zs_test.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/fuzz_parse: test/fuzz_parse.c $(LIB) $(ZXV_LIB)
	$(TESTCC) -DZS_FUZZ_MAIN $< $(LIB) $(LINK) -o $@

$(OUT)/sutra_demo: examples/sutra_demo.c $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

test: check-nofloat $(addprefix $(OUT)/,$(TESTS)) $(OUT)/fuzz_parse $(OUT)/zsutra
	@for t in $(TESTS); do $(OUT)/$$t || exit 1; done
	$(OUT)/fuzz_parse 3000 examples/*.cpy examples/*.sutra
	$(OUT)/zsutra same examples/payment.cpy examples/payment.sutra
	$(OUT)/zsutra same examples/statement.cpy examples/statement.sutra
	@if $(OUT)/zsutra layout examples/broken.cpy; then echo "broken.cpy parsed"; exit 1; fi
	@if $(OUT)/zsutra layout examples/broken.sutra; then echo "broken.sutra parsed"; exit 1; fi
	@echo "cobol-sutra: all tests passed"

demo: $(OUT)/sutra_demo
	$(OUT)/sutra_demo examples/payment.cpy examples/payment.sutra

fuzz: $(OUT)/fuzz_parse
	$(OUT)/fuzz_parse $(FUZZ_ITERS) examples/*.cpy examples/*.sutra

fuzz-libfuzzer:
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  mkdir -p $(OUT)/corpus && cp examples/*.cpy examples/*.sutra $(OUT)/corpus/ && \
	  clang -std=gnu11 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude -Isrc \
	    -I$(ZXV)/include test/fuzz_parse.c $(SRC) \
	    -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -o $(OUT)/fuzz_libfuzzer && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=$(LIBFUZZER_SECONDS) $(OUT)/corpus; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

# ---- GnuCOBOL interop ----
# IBM-style binary sizes (2-4-8 bytes) and big-endian COMP, as on a mainframe.
COBFLAGS := -x -fbinary-size=2-4-8 -fbinary-byteorder=big-endian -I examples
COB_SHIM := cobol/zscob_shim.c

$(OUT)/paypost: cobol/PAYPOST.cbl $(COB_SHIM) examples/payment.cpy $(LIB) $(ZXV_LIB)
	@mkdir -p $(OUT)
	$(COBC) $(COBFLAGS) -o $@ cobol/PAYPOST.cbl $(COB_SHIM) \
	  -A "-Iinclude -I$(ZXV)/include" $(LIB) -L$(ZXV_ABS) -lzxv -Q "-Wl,-rpath,$(ZXV_ABS)"

cobol: $(OUT)/paypost
	cd $(OUT) && ./paypost $(abspath examples/payment.cpy) > paypost.out
	grep -v '^#' cobol/paypost.expected | diff -u - $(OUT)/paypost.out
	@echo "GnuCOBOL interop: output matches cobol/paypost.expected"

# GOLDEN.cbl writes the same records with the ASCII and the EBCDIC sign
# convention; both must equal the checked-in hex.
golden-cobol:
	@mkdir -p $(OUT)
	$(COBC) $(COBFLAGS) -o $(OUT)/golden-ascii cobol/GOLDEN.cbl cobol/golden_hex.c
	$(COBC) $(COBFLAGS) -fsign=EBCDIC -o $(OUT)/golden-ibm cobol/GOLDEN.cbl cobol/golden_hex.c
	$(OUT)/golden-ascii > $(OUT)/records-ascii.hex
	$(OUT)/golden-ibm > $(OUT)/records-ibm.hex
	grep -v '^#' test/golden/records-ascii.hex | diff -u - $(OUT)/records-ascii.hex
	grep -v '^#' test/golden/records-ibm.hex | diff -u - $(OUT)/records-ibm.hex
	@echo "GnuCOBOL golden records: identical to test/golden"

# R11: no float type anywhere in the pillar (the z-wasm kit's checker;
# comments and strings are excluded).
check-nofloat:
	$(PYTHON) ../../kits/z-wasm/tools/check_nofloat.py $(SRC) src/zs_internal.h include/zsutra.h \
	  tools/zsutra_tool.c cobol/zscob_shim.c
	@echo "compiler guard for the pillar sources: $(or $(NOFLOAT),none on this compiler)"

clean:
	rm -rf $(OUT)
