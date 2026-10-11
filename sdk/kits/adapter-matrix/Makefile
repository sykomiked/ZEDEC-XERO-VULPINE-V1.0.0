# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/kits/adapter-matrix — ingest external records (JSON over HTTP, MQTT
# payloads, FIX tag=value) and label each one with a trust state.
#
#   make                 build/libzadapter.a and build/za-tool
#   make test            adapter, decimal and validation tests, a fuzz smoke run
#                        and the no-float check
#   make demo            ingest, validate and attest the example payloads
#   make fuzz            the deterministic adapter fuzzer (FUZZ_ITERS=...)
#   make fuzz-libfuzzer  the libFuzzer target, if clang ships libFuzzer
#   make check-nofloat   R11: no float type in the kit's sources
#   make clean
#
# Options:
#   SANITIZE=1           -fsanitize=address,undefined for the kit, z-contract
#                        (rebuilt under $(OUT)/zcontract) and the tests
#
# No test opens a network connection: every payload is bytes in a C source
# (tests, examples/ingest_demo.c, the fuzzer's built-in seeds).

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../../libzxv
ZC      := ../z-contract
ZW      := ../z-wasm

ZXV_ABS := $(abspath $(ZXV)/build)
ifeq ($(shell uname -s),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
endif
ZC_OUT  := $(abspath $(OUT))/zcontract
ZC_LIB  := $(ZC_OUT)/libzcontract.a

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) \
           -Iinclude -Isrc -I$(ZXV)/include -I$(ZC)/include

SRC  := src/za_record.c src/za_decimal.c src/za_json.c src/za_mqtt.c src/za_fix.c src/za_validate.c
OBJ  := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRC))
LIB  := $(OUT)/libzadapter.a
HDRS := include/zadapter.h src/za_internal.h $(ZXV)/include/zxv_api.h $(ZC)/include/zcontract.h

LINK  := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -lm $(SAN)
TESTCC = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) -Iinclude -Isrc -I$(ZXV)/include -I$(ZC)/include -Itest
LIBS   = $(LIB) $(ZC_LIB)

FUZZ_ITERS ?= 20000

.PHONY: all test demo fuzz fuzz-libfuzzer check-nofloat clean

all: $(LIB) $(OUT)/za-tool

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(ZC_LIB): $(wildcard $(ZC)/src/*.c $(ZC)/src/*.h $(ZC)/include/*.h)
	$(MAKE) -C $(ZC) CC=$(CC) SANITIZE=$(SANITIZE) OUT=$(ZC_OUT) $(ZC_LIB)

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/za-tool: tools/za_tool.c $(LIBS) $(ZXV_LIB) $(HDRS)
	$(TESTCC) $< $(LIBS) $(LINK) -o $@

$(OUT)/test_adapters: test/test_adapters.c test/za_test.h $(LIBS) $(ZXV_LIB)
	$(TESTCC) $< $(LIBS) $(LINK) -o $@

$(OUT)/test_validate: test/test_validate.c test/za_test.h $(LIBS) $(ZXV_LIB)
	$(TESTCC) $< $(LIBS) $(LINK) -o $@

$(OUT)/fuzz_adapters: test/fuzz_adapters.c $(LIBS) $(ZXV_LIB)
	$(TESTCC) -DZA_FUZZ_MAIN $< $(LIBS) $(LINK) -o $@

$(OUT)/ingest_demo: examples/ingest_demo.c $(LIBS) $(ZXV_LIB) $(HDRS)
	$(TESTCC) $< $(LIBS) $(LINK) -o $@

test: check-nofloat $(OUT)/test_adapters $(OUT)/test_validate $(OUT)/fuzz_adapters $(OUT)/za-tool $(OUT)/ingest_demo
	$(OUT)/test_adapters
	$(OUT)/test_validate examples/market.zc
	$(OUT)/fuzz_adapters 3000
	$(OUT)/ingest_demo examples/market.zc > /dev/null

demo: $(OUT)/ingest_demo $(OUT)/za-tool
	$(OUT)/ingest_demo examples/market.zc
	@printf '{"value": 151, "unit": "C"}' > $(OUT)/reading.json
	-$(OUT)/za-tool mqtt $(OUT)/reading.json examples/market.zc reading plant/7/boiler

fuzz: $(OUT)/fuzz_adapters
	$(OUT)/fuzz_adapters $(FUZZ_ITERS)

fuzz-libfuzzer: $(ZC_LIB)
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  mkdir -p $(OUT)/corpus && \
	  clang -std=gnu11 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude -Isrc -I$(ZXV)/include \
	    -I$(ZC)/include test/fuzz_adapters.c $(SRC) $(ZC)/src/zc_util.c $(ZC)/src/zc_parse.c \
	    $(ZC)/src/zc_check.c $(ZC)/src/zc_evlog.c $(ZC)/src/zc_hexsink.c -I$(ZC)/src \
	    -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -lm -o $(OUT)/fuzz_libfuzzer && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=60 $(OUT)/corpus; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

check-nofloat:
	$(PYTHON) $(ZW)/tools/check_nofloat.py $(SRC) src/za_internal.h include/zadapter.h tools/za_tool.c \
	  examples/ingest_demo.c test/*.c test/*.h
	@echo "compiler guard for the kit sources: $(or $(NOFLOAT),none on this compiler)"

clean:
	rm -rf $(OUT)
