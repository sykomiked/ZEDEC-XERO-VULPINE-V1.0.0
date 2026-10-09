# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# bindings/dotnet/native — builds libzxv (the zxv_api.h C ABI) as a shared
# library for the .NET SDK, plus a C conformance test of the ABI itself.
#
#   make            build build/libzxv.so (libzxv.dylib on macOS)
#   make test       build and run the C ABI test
#   make clean
#
# Optional kernel modules are detected by header presence, so this Makefile
# keeps working while kernel/src/cbank and kernel/src/cardnet are in flight:
#   kernel/src/cbank/cb_ccy.h       -> -DZXV_HAVE_CBANK_CCY (ISO 4217 table)
#   kernel/src/cardnet/cn_check.h   -> -DZXV_HAVE_CN_CHECK  (PAN check digits)
# Force one off with e.g. `make ZXV_NO_CBANK=1`.

ifeq ($(origin CC),default)
  CC := gcc
endif
K       := ../../../kernel
OUT     := build
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  LIB    := $(OUT)/libzxv.dylib
  SOFLAG := -dynamiclib -install_name @rpath/libzxv.dylib
else
  LIB    := $(OUT)/libzxv.so
  SOFLAG := -shared -Wl,-soname,libzxv.so -Wl,-z,defs -Wl,-z,now -Wl,-z,relro
endif

# TEST_HOST selects the hosted representation of the kernel number types
# (surplus_real_t = double). See the exactness note at the top of zxv_api.c.
CFLAGS_COMMON := -std=c11 -O2 -g -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
                 -fstack-protector-strong -D_FORTIFY_SOURCE=2 -DTEST_HOST
INC := -I. -I$(K)/src/iso20022 -I$(K)/src/finance -I$(K)/src/vino_stores \
       -I$(K)/src/vino -I$(K)/src/surplus -I$(K)/src/edp_risk -I$(K)/src/onepolicy \
       -I$(K)/include

KSRC := $(K)/src/iso20022/iso20022.c $(K)/src/finance/triple_ledger.c
DEFS :=

ifeq ($(ZXV_NO_CBANK),)
ifneq ($(wildcard $(K)/src/cbank/cb_ccy.h),)
  DEFS += -DZXV_HAVE_CBANK_CCY
  INC  += -I$(K)/src/cbank -I$(K)/src/tensor
  KSRC += $(K)/src/cbank/cb_ccy.c $(K)/src/cbank/cb_ccy_tbl.c $(K)/src/cbank/cb_util.c \
          $(K)/src/tensor/zt.c
endif
endif

ifeq ($(ZXV_NO_CARDNET),)
ifneq ($(wildcard $(K)/src/cardnet/cn_check.h),)
  DEFS += -DZXV_HAVE_CN_CHECK
  INC  += -I$(K)/src/cardnet
  KSRC += $(K)/src/cardnet/cn_check.c
endif
endif

KOBJ := $(patsubst $(K)/src/%.c,$(OUT)/k/%.o,$(KSRC))

.PHONY: all test clean print-config

all: $(LIB)

$(OUT)/k/%.o: $(K)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) $(INC) -c $< -o $@

$(OUT)/zxv_api.o: zxv_api.c zxv_api.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS_COMMON) -DZXV_BUILD_SHARED $(DEFS) $(INC) -c $< -o $@

$(LIB): $(OUT)/zxv_api.o $(KOBJ)
	$(CC) $(SOFLAG) -o $@ $^ -lpthread -lm

$(OUT)/test_zxv_api: test_zxv_api.c zxv_api.h $(LIB)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -I. $< -L$(OUT) -lzxv -Wl,-rpath,'$$ORIGIN' -lpthread -o $@

test: $(OUT)/test_zxv_api
	./$(OUT)/test_zxv_api

print-config:
	@echo "LIB=$(LIB)"; echo "DEFS=$(DEFS)"; echo "KSRC=$(KSRC)"

clean:
	rm -rf $(OUT)
