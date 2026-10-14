# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# tri-witness — an offline, read-only replay of Sutra-Clear exports.
# The library shares no code with sutra-clear; the tests, the fuzzer and the
# example link sutra-clear's library (built here into $(OUT)/sc) to make
# real exports to check.

KIT        := tri-witness
LIBNAME    := ztwit
SRC        := src/ztw_parse.c src/ztw_verify.c
HDRS       := include/ztwit.h src/ztw_internal.h
TESTS      := test_twit
FUZZ       := fuzz_twit
EXAMPLES   := audit_demo
FUZZ_SMOKE := 300

include ../mk/kit.mk

SC_DIR  := ../sutra-clear
SC_SRC  := $(wildcard $(SC_DIR)/src/*.c)
SC_HDRS := $(SC_DIR)/include/zsclear.h $(SC_DIR)/src/zsc_internal.h
SC_OBJ  := $(patsubst $(SC_DIR)/src/%.c,$(OUT)/sc/%.o,$(SC_SRC))
SC_LIB  := $(OUT)/sc/libzsclear.a
SC_INC  := -I$(SC_DIR)/include

$(OUT)/sc/%.o: $(SC_DIR)/src/%.c $(SC_HDRS) $(ZIND_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -I$(SC_DIR)/src $(SC_INC) -c $< -o $@

$(SC_LIB): $(SC_OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/test_twit: test/test_twit.c $(LIBS) $(SC_LIB) $(ZXV_LIB) $(DEPS) $(SC_HDRS)
	@mkdir -p $(OUT)
	$(TESTCC) $(SC_INC) $< $(LIB) $(SC_LIB) $(ZIND_LIB) $(LINK) -o $@

$(OUT)/fuzz_twit: test/fuzz_twit.c $(LIBS) $(SC_LIB) $(ZXV_LIB) $(DEPS) $(SC_HDRS)
	@mkdir -p $(OUT)
	$(TESTCC) $(SC_INC) $< $(LIB) $(SC_LIB) $(ZIND_LIB) $(LINK) -o $@

$(OUT)/audit_demo: examples/audit_demo.c $(LIBS) $(SC_LIB) $(ZXV_LIB) $(DEPS) $(SC_HDRS)
	@mkdir -p $(OUT)
	$(TESTCC) $(SC_INC) $< $(LIB) $(SC_LIB) $(ZIND_LIB) $(LINK) -o $@
