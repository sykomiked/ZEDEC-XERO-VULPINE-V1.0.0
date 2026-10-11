# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/libzxv — builds libzxv, the one C ABI (include/zxv_api.h) that every
# ZXV SDK wraps, from the kernel sources in place (nothing is copied).
#
#   make                 build/libzxv.so (libzxv.dylib on macOS) + build/libzxv.a
#   make test            build and run the C conformance tests
#   make vectors         regenerate ../conformance/vectors.json (shared by all SDKs)
#   make fixtures        write ../conformance/fixtures/*.gguf (a tiny test model)
#   make dist            stage build/dist/{include,lib} for packaging
#   make clean
#
# Options:
#   ZXV_NO_PQC=1      leave out ML-KEM / ML-DSA / Ed25519 (calls return NOTIMPL)
#   ZXV_NO_TENSOR=1   leave out the inference engine
#   ZXV_NO_CBANK=1    leave out the ISO 4217 table (see src/zxv_api.c)
#   SANITIZE=1        build with -fsanitize=address,undefined (tests)
#
# Optional kernel modules are detected by header presence, so this Makefile
# keeps working while kernel/src/cbank and kernel/src/cardnet are in flight.

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
K       := ../../kernel
OUT     := build
# The Android NDK has pthreads in libc and ships no libpthread: build with
# PTHREAD= there.
PTHREAD ?= -lpthread
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  LIB    := $(OUT)/libzxv.dylib
  SOFLAG := -dynamiclib -install_name @rpath/libzxv.dylib
  RPATH  := -Wl,-rpath,@loader_path
  # _POSIX_C_SOURCE in the sources would otherwise hide arc4random_buf.
  OSDEFS := -D_DARWIN_C_SOURCE
else
  LIB    := $(OUT)/libzxv.so
  SOFLAG := -shared -Wl,-soname,libzxv.so -Wl,-z,defs -Wl,-z,now -Wl,-z,relro
  RPATH  := -Wl,-rpath,'$$ORIGIN'
endif
STATIC := $(OUT)/libzxv.a

ifneq ($(SANITIZE),)
  # shift-base: the vendored ref10 Ed25519 left-shifts negative limbs, which
  # every supported compiler defines as two's complement (and C23 does too).
  SAN := -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all \
         -fno-omit-frame-pointer
endif

# TEST_HOST selects the hosted representation of the kernel number types
# for the triple ledger only (see the exactness note in src/zxv_api.c).
CFLAGS_COMMON := -std=c11 -O2 -g -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
                 -fstack-protector-strong -D_FORTIFY_SOURCE=2 -DTEST_HOST $(OSDEFS) $(SAN)
# Kernel files are third-party to this build: no -Werror on them.
KFLAGS := -std=c11 -O2 -g -fPIC -fvisibility=hidden -DTEST_HOST $(OSDEFS) $(SAN) -w

INC := -Iinclude -Isrc -I$(K)/src/iso20022 -I$(K)/src/finance -I$(K)/src/vino_stores \
       -I$(K)/src/vino -I$(K)/src/surplus -I$(K)/src/edp_risk -I$(K)/src/onepolicy \
       -I$(K)/include -I$(K)/src/rational -I$(K)/src/lpres -I$(K)/src/event_space \
       -I$(K)/src/rmag -I$(K)/src/phase_coord -I$(K)/src/harmonic -I$(K)/src/swarm \
       -I$(K)/src/mlkem -I$(K)/src/e8 -I$(K)/src/modbind -I$(K)/src/trispace

KSRC := $(K)/src/iso20022/iso20022.c $(K)/src/finance/triple_ledger.c \
        $(K)/src/rational/rational.c $(K)/src/lpres/lpres.c $(K)/src/rmag/rmag.c \
        $(K)/src/phase_coord/phase_coord.c $(K)/src/harmonic/zt_harmonic_wire.c \
        $(K)/src/swarm/swarm_budget.c $(K)/src/swarm/swarm_emotion.c \
        $(K)/src/mlkem/keccak.c $(K)/src/lpres/lpres_hex.c $(K)/src/robin_debanks/sha256.c
SRC  := src/zxv_api.c src/zxv_core.c src/zxv_econ.c src/zxv_crypto.c src/zxv_tensor.c \
        src/zxv_evm.c src/zxv_hex.c
DEFS :=

ifeq ($(ZXV_NO_PQC),)
  INC  += -I$(K)/src/pqsec -I$(K)/third_party/ed25519/src
  KSRC += $(K)/src/pqsec/pq_mlkem1024.c $(K)/src/pqsec/pq_mldsa87.c $(K)/src/pqsec/mldsa/fips202.c \
          $(wildcard $(K)/third_party/ed25519/src/*.c)
else
  DEFS += -DZXV_NO_PQC
endif

ifeq ($(ZXV_NO_TENSOR),)
  INC  += -I$(K)/src/tensor
  KSRC += $(addprefix $(K)/src/tensor/,zt.c zt_gguf.c zt_tok.c zt_rope.c zt_model.c \
          zt_simd.c zt_simd_x86.c zt_simd_neon.c)
else
  DEFS += -DZXV_NO_TENSOR
endif

ifeq ($(ZXV_NO_CBANK),)
ifneq ($(wildcard $(K)/src/cbank/cb_ccy.h),)
  DEFS += -DZXV_HAVE_CBANK_CCY
  INC  += -I$(K)/src/cbank -I$(K)/src/tensor
  KSRC += $(K)/src/cbank/cb_ccy.c $(K)/src/cbank/cb_ccy_tbl.c $(K)/src/cbank/cb_util.c
  ifneq ($(ZXV_NO_TENSOR),)
    KSRC += $(K)/src/tensor/zt.c
  endif
endif
endif

ifeq ($(ZXV_NO_CARDNET),)
ifneq ($(wildcard $(K)/src/cardnet/cn_check.h),)
  DEFS += -DZXV_HAVE_CN_CHECK
  INC  += -I$(K)/src/cardnet
  KSRC += $(K)/src/cardnet/cn_check.c
endif
endif

KSRC := $(sort $(KSRC))
KOBJ := $(patsubst $(K)/%.c,$(OUT)/k/%.o,$(KSRC))
OBJ  := $(patsubst src/%.c,$(OUT)/%.o,$(SRC))

.PHONY: all test vectors fixtures dist clean print-config

all: $(LIB) $(STATIC)

$(OUT)/k/%.o: $(K)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KFLAGS) -MMD -MP $(INC) -c $< -o $@

$(OUT)/%.o: src/%.c include/zxv_api.h src/zxv_internal.h src/zxv_q16.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS_COMMON) -MMD -MP -DZXV_BUILD_SHARED $(DEFS) $(INC) -c $< -o $@

$(LIB): $(OBJ) $(KOBJ)
	$(CC) $(SOFLAG) $(SAN) -o $@ $^ $(PTHREAD) -lm

$(STATIC): $(OBJ) $(KOBJ)
	@rm -f $@
	$(AR) rcs $@ $^

# Header dependencies (kernel headers included), so an incremental build
# after a kernel merge never links stale objects.
-include $(OBJ:.o=.d) $(KOBJ:.o=.d)

TESTCC := $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude $(SAN)
$(OUT)/test_zxv_api: test/test_zxv_api.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(PTHREAD) -o $@
$(OUT)/test_zxv_abi11: test/test_zxv_abi11.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(PTHREAD) -o $@
$(OUT)/test_zxv_abi12: test/test_zxv_abi12.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(PTHREAD) -o $@
$(OUT)/test_zxv_abi13: test/test_zxv_abi13.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(PTHREAD) -o $@
# The portable (no __int128) Q16 rounding, forced with -DZXV_NO_INT128 and
# checked against the 128-bit path and the library.
$(OUT)/test_q16: test/test_q16.c src/zxv_q16.h include/zxv_api.h $(LIB)
	$(TESTCC) -DZXV_NO_INT128 -Isrc $< -L$(OUT) -lzxv $(RPATH) $(PTHREAD) -o $@
$(OUT)/gen_vectors: tools/gen_vectors.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(PTHREAD) -o $@
$(OUT)/mkfixture: tools/mkfixture.c
	$(CC) -std=c11 -O1 -I$(K)/src/tensor -o $@ $<

test: $(OUT)/test_zxv_api $(OUT)/test_zxv_abi11 $(OUT)/test_zxv_abi12 $(OUT)/test_zxv_abi13 \
      $(OUT)/test_q16 fixtures
	./$(OUT)/test_zxv_api
	./$(OUT)/test_zxv_abi11 ../conformance/fixtures
	./$(OUT)/test_zxv_abi12 ../conformance/fixtures
	./$(OUT)/test_zxv_abi13
	./$(OUT)/test_q16

fixtures: $(OUT)/mkfixture
	@mkdir -p ../conformance/fixtures
	./$(OUT)/mkfixture ../conformance/fixtures

vectors: $(OUT)/gen_vectors fixtures
	./$(OUT)/gen_vectors ../conformance/fixtures > ../conformance/vectors.json

dist: all
	@rm -rf $(OUT)/dist && mkdir -p $(OUT)/dist/include $(OUT)/dist/lib
	cp include/zxv_api.h $(OUT)/dist/include/
	cp $(LIB) $(STATIC) $(OUT)/dist/lib/

print-config:
	@echo "LIB=$(LIB)"; echo "DEFS=$(DEFS)"; echo "KSRC=$(KSRC)"

clean:
	rm -rf $(OUT)
