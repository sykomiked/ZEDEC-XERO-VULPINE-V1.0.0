# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/pillars/mumps-myra — M (MUMPS) globals and MYRA over libzxv.
#
#   make                 build/libzmyra.a and build/zmyra (the command-line tool)
#   make test            unit tests, golden schemas and records, collation tests,
#                        the posting bridge, a fuzz smoke run and the no-float check
#   make demo            the posting demo
#   make fuzz            the deterministic fuzzer (FUZZ_ITERS=..., ZM_FUZZ_SEED=n)
#   make fuzz-libfuzzer  the libFuzzer target, if clang ships libFuzzer
#   make m               GT.M / YottaDB interop: build the call-out library, run
#                        m/ZMYRATST.m against a real database, compare its output,
#                        and cross-check collation, canonical numbers and $H dates
#                        with the M engine (needs GT.M or YottaDB; see GTM_DIST)
#   make golden-m        regenerate test/golden/*.m-expect with the M engine and
#                        compare them with the checked-in files
#   make check-nofloat   R11: no float type in the pillar's sources
#   make clean
#
# Options:
#   SANITIZE=1           -fsanitize=address,undefined for the pillar and its tests
#   OUT=dir              build directory (default build/, git-ignored)
#   GTM_DIST=dir         the GT.M or YottaDB install (default: $gtm_dist, $ydb_dist,
#                        or Debian/Ubuntu's fis-gtm package)
#
# libzxv is built with `make -C ../../libzxv all`.

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
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

SRC  := src/zm_util.c src/zm_num.c src/zm_lex.c src/zm_schema.c src/zm_myra.c src/zm_gmap.c \
        src/zm_ref.c src/zm_record.c src/zm_post.c
OBJ  := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRC))
LIB  := $(OUT)/libzmyra.a
HDRS := include/zmyra.h src/zm_internal.h $(ZXV)/include/zxv_api.h

LINK  := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread $(SAN)
TESTCC = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) -Iinclude -Isrc -I$(ZXV)/include -Itest

TESTS := test_number test_schema test_record test_collate test_golden test_post
FUZZ_ITERS ?= 20000
LIBFUZZER_SECONDS ?= 60

.PHONY: all test demo fuzz fuzz-libfuzzer m golden-m check-nofloat clean

all: $(LIB) $(OUT)/zmyra

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/zmyra: tools/zmyra_tool.c $(LIB) $(ZXV_LIB) $(HDRS)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_%: test/test_%.c test/zm_test.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/fuzz_parse: test/fuzz_parse.c $(LIB) $(ZXV_LIB)
	$(TESTCC) -DZM_FUZZ_MAIN $< $(LIB) $(LINK) -o $@

$(OUT)/myra_demo: examples/myra_demo.c $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

test: check-nofloat $(addprefix $(OUT)/,$(TESTS)) $(OUT)/fuzz_parse $(OUT)/zmyra
	@for t in $(TESTS); do $(OUT)/$$t || exit 1; done
	$(OUT)/fuzz_parse 3000 examples/*.gmap examples/*.myra
	$(OUT)/zmyra same examples/payment.gmap examples/payment.myra
	$(OUT)/zmyra same examples/stock.gmap examples/stock.myra
	@if $(OUT)/zmyra schema examples/broken.gmap; then echo "broken.gmap parsed"; exit 1; fi
	@if $(OUT)/zmyra schema examples/broken.myra; then echo "broken.myra parsed"; exit 1; fi
	grep -v '^#' test/golden/collate.in | $(OUT)/zmyra collate > $(OUT)/collate.out
	grep -v '^#' test/golden/collate.m-expect | diff -u - $(OUT)/collate.out
	@echo "mumps-myra: all tests passed"

demo: $(OUT)/myra_demo
	$(OUT)/myra_demo examples/payment.gmap examples/payment.myra

fuzz: $(OUT)/fuzz_parse
	$(OUT)/fuzz_parse $(FUZZ_ITERS) examples/*.gmap examples/*.myra

fuzz-libfuzzer:
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  mkdir -p $(OUT)/corpus && cp examples/*.gmap examples/*.myra $(OUT)/corpus/ && \
	  clang -std=gnu11 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude -Isrc \
	    -I$(ZXV)/include test/fuzz_parse.c $(SRC) \
	    -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -o $(OUT)/fuzz_libfuzzer && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=$(LIBFUZZER_SECONDS) $(OUT)/corpus; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

# ---- GT.M / YottaDB interop ----
GTM_DIST ?= $(or $(gtm_dist),$(ydb_dist),$(firstword $(wildcard /usr/lib/x86_64-linux-gnu/fis-gtm/V*_x86_64 /usr/lib/aarch64-linux-gnu/fis-gtm/V*_aarch64)))
MOUT     := $(abspath $(OUT))/m
MENV      = gtm_dist=$(GTM_DIST) ydb_dist=$(GTM_DIST) \
            gtmroutines="$(MOUT)($(abspath m)) $(GTM_DIST)/libgtmutil.so" \
            ydb_routines="$(MOUT)($(abspath m)) $(GTM_DIST)/libgtmutil.so" \
            gtmgbldir=$(MOUT)/zmyra.gld ydb_gbldir=$(MOUT)/zmyra.gld \
            GTMXC_zmyra=$(MOUT)/zmyra.xc ydb_xc_zmyra=$(MOUT)/zmyra.xc \
            ZMYRA_DIR=$(abspath .)
MUMPS     = $(MENV) $(GTM_DIST)/mumps

$(MOUT)/libzmyra_xc.so: m/zmyra_xc.c $(SRC) $(HDRS)
	@test -n "$(GTM_DIST)" -a -f "$(GTM_DIST)/gtmxc_types.h" || \
	  { echo "no GT.M or YottaDB found; set GTM_DIST"; exit 1; }
	@mkdir -p $(MOUT)
	$(CC) -std=c11 -O2 -g -shared -fPIC $(WARN) $(SAN) $(NOFLOAT) -Iinclude -Isrc -I$(ZXV)/include \
	  -isystem $(GTM_DIST) m/zmyra_xc.c $(SRC) -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -o $@

$(MOUT)/zmyra.xc: m/zmyra.xc.in $(MOUT)/libzmyra_xc.so
	sed -e '/^#/d' -e 's|@LIB@|$(MOUT)/libzmyra_xc.so|' m/zmyra.xc.in > $@

# A fresh database for each run: build/m/zmyra.gld and zmyra.dat.
$(MOUT)/zmyra.dat: $(MOUT)/zmyra.xc
	@rm -f $(MOUT)/zmyra.gld $(MOUT)/zmyra.dat
	@printf 'change -segment DEFAULT -file_name=$(MOUT)/zmyra.dat\nexit\n' | \
	  $(MENV) $(GTM_DIST)/mumps -run GDE > $(MOUT)/gde.log 2>&1
	@$(MENV) $(GTM_DIST)/mupip create > $(MOUT)/mupip.log 2>&1 || { cat $(MOUT)/gde.log $(MOUT)/mupip.log; exit 1; }
	@echo "database: $(MOUT)/zmyra.dat"

HDR = printf '\# %s -- written by GT.M/YottaDB ($(notdir $(GTM_DIST))) through m/ZMYRAGLD.m; regenerate with make golden-m.\n\# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC\n\# SPDX-License-Identifier: Apache-2.0\n'

# What the M engine itself decides: collation ($ORDER of one local array),
# canonical numbers (x=+x) and $ZDATE. Written to build/m/*.m.
$(MOUT)/engine.stamp: $(MOUT)/zmyra.dat m/ZMYRAGLD.m test/golden/collate.in test/golden/canonic.in
	@grep -v '^#' test/golden/collate.in | (cd $(MOUT) && $(MUMPS) -run sort^ZMYRAGLD) > $(MOUT)/collate.m
	@grep -v '^#' test/golden/canonic.in | (cd $(MOUT) && $(MUMPS) -run canon^ZMYRAGLD) > $(MOUT)/canonic.m
	@cd $(MOUT) && $(MUMPS) -run dates^ZMYRAGLD > dates.m
	@touch $@

m: $(LIB) $(OUT)/zmyra $(MOUT)/engine.stamp
	@echo "M engine: $(GTM_DIST)"
	@cd $(MOUT) && $(MUMPS) -run ^ZMYRATST > ZMYRATST.out 2>&1 || { cat ZMYRATST.out; exit 1; }
	grep -v '^#' m/ZMYRATST.expected | diff -u - $(MOUT)/ZMYRATST.out
	@echo "M interop: m/ZMYRATST.m output matches m/ZMYRATST.expected"
	grep -v '^#' test/golden/collate.m-expect | diff -u - $(MOUT)/collate.m
	grep -v '^#' test/golden/canonic.m-expect | diff -u - $(MOUT)/canonic.m
	grep -v '^#' test/golden/dates.m-expect | diff -u - $(MOUT)/dates.m
	@echo "M engine output equals test/golden/*.m-expect"
	@cd $(MOUT) && $(MUMPS) -run cross^ZMYRAGLD > cross.out 2>&1 || { cat cross.out; exit 1; }
	@cat $(MOUT)/cross.out
	@grep -q '^cross-check: 0 differences' $(MOUT)/cross.out
	@echo "M engine and libzmyra agree on collation, canonical numbers and \$$H dates"

golden-m: $(MOUT)/engine.stamp
	{ $(HDR) collate.m-expect; cat $(MOUT)/collate.m; } > test/golden/collate.m-expect
	{ $(HDR) canonic.m-expect; cat $(MOUT)/canonic.m; } > test/golden/canonic.m-expect
	{ $(HDR) dates.m-expect; cat $(MOUT)/dates.m; } > test/golden/dates.m-expect
	@git diff --stat -- test/golden 2>/dev/null || true

# R11: no float type anywhere in the pillar (the z-wasm kit's checker;
# comments and strings are excluded).
check-nofloat:
	$(PYTHON) ../../kits/z-wasm/tools/check_nofloat.py $(SRC) src/zm_internal.h include/zmyra.h \
	  tools/zmyra_tool.c m/zmyra_xc.c examples/myra_demo.c
	@echo "compiler guard for the pillar sources: $(or $(NOFLOAT),none on this compiler)"

clean:
	rm -rf $(OUT)
