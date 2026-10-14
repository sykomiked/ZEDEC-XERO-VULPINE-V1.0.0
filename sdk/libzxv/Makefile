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
#   ZXV_NO_CBANK=1    leave out the ISO 4217 table and netting cycles (see src/zxv_api.c)
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

# The target, not the build machine, picks the library form: a MinGW or
# zig cross compiler (CC=x86_64-w64-mingw32-gcc, CC="zig cc -target
# x86_64-windows-gnu") makes libzxv.dll with its import library.
TARGET_OS := $(shell $(CC) -dumpmachine 2>/dev/null)

ifneq ($(findstring mingw,$(TARGET_OS))$(findstring windows,$(TARGET_OS)),)
  LIB    := $(OUT)/libzxv.dll
  # locks come from src/win/pthread.h, so the DLL needs no winpthreads
  SOFLAG := -shared -Wl,--out-implib,$(OUT)/libzxv.dll.a -static-libgcc
  RPATH  :=
  OSLIBS := -lbcrypt
  OSDEFS := -D__USE_MINGW_ANSI_STDIO=1
  # no PIC on PE, and _FORTIFY_SOURCE would need MinGW's libssp
  PICFLAG :=
  FORTIFY :=
  EXE    := .exe
  WININC := -Isrc/win
  # the multi-thread tests use MinGW's winpthreads, linked in statically
  TESTLIBS := -static-libgcc -Wl,-Bstatic -lpthread -Wl,-Bdynamic
else ifeq ($(UNAME_S),Darwin)
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
# RUN runs a test binary built for another OS, e.g. RUN=wine for a
# Windows cross build; HOSTCC builds the fixture writer for this machine.
RUN    ?=
HOSTCC ?= $(if $(EXE),cc,$(CC))
TESTLIBS ?= $(PTHREAD)
PICFLAG ?= -fPIC
FORTIFY ?= -D_FORTIFY_SOURCE=2

ifneq ($(SANITIZE),)
  # shift-base: the vendored ref10 Ed25519 left-shifts negative limbs, which
  # every supported compiler defines as two's complement (and C23 does too).
  SAN := -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all \
         -fno-omit-frame-pointer
endif

# TEST_HOST selects the hosted representation of the kernel number types
# for the triple ledger only (see the exactness note in src/zxv_api.c).
CFLAGS_COMMON := -std=c11 -O2 -g $(PICFLAG) -fvisibility=hidden -Wall -Wextra -Werror \
                 -fstack-protector-strong $(FORTIFY) -DTEST_HOST $(OSDEFS) $(SAN)
# Kernel files are third-party to this build: no -Werror on them.
KFLAGS := -std=c11 -O2 -g $(PICFLAG) -fvisibility=hidden -DTEST_HOST $(OSDEFS) $(SAN) -w

INC := -Iinclude -Isrc $(WININC) -I$(K)/src/iso20022 -I$(K)/src/finance -I$(K)/src/vino_stores \
       -I$(K)/src/vino -I$(K)/src/surplus -I$(K)/src/edp_risk -I$(K)/src/onepolicy \
       -I$(K)/include -I$(K)/src/rational -I$(K)/src/lpres -I$(K)/src/event_space \
       -I$(K)/src/rmag -I$(K)/src/phase_coord -I$(K)/src/harmonic -I$(K)/src/swarm \
       -I$(K)/src/mlkem -I$(K)/src/e8 -I$(K)/src/modbind -I$(K)/src/trispace \
       -I$(K)/src/hwemu -I$(K)/src/robin_debanks

KSRC := $(K)/src/iso20022/iso20022.c $(K)/src/finance/triple_ledger.c \
        $(K)/src/rational/rational.c $(K)/src/lpres/lpres.c $(K)/src/rmag/rmag.c \
        $(K)/src/phase_coord/phase_coord.c $(K)/src/harmonic/zt_harmonic_wire.c \
        $(K)/src/swarm/swarm_budget.c $(K)/src/swarm/swarm_emotion.c \
        $(K)/src/mlkem/keccak.c $(K)/src/lpres/lpres_hex.c $(K)/src/robin_debanks/sha256.c \
        $(K)/src/hwemu/hwm.c $(K)/src/hwemu/hwp.c $(K)/src/hwemu/hwr.c \
        $(K)/src/hwemu/hwa.c $(K)/src/hwemu/hwe.c
SRC  := src/zxv_api.c src/zxv_core.c src/zxv_econ.c src/zxv_crypto.c src/zxv_tensor.c \
        src/zxv_evm.c src/zxv_hex.c src/zxv_hw.c src/zxv_abi15.c \
        src/zxv_treaty.c src/zxv_aipi.c src/zxv_coin.c src/zxv_fee.c src/zxv_vhub.c src/zxv_freight.c src/zxv_web4.c src/zxv_paradox.c \
        src/zxv_vino.c
DEFS :=

ifeq ($(ZXV_NO_PQC),)
  INC  += -I$(K)/src/pqsec -I$(K)/third_party/ed25519/src
  KSRC += $(K)/src/pqsec/pq_mlkem1024.c $(K)/src/pqsec/pq_mldsa87.c $(K)/src/pqsec/mldsa/fips202.c \
          $(K)/src/hwemu/hwr_pq.c $(K)/src/pqsec/pq_slh256s.c \
          $(sort $(wildcard $(K)/src/pqsec/slhdsa/*.c)) \
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
  KFLAGS += -DHWE_NO_TENSOR
endif

ifeq ($(ZXV_NO_CBANK),)
ifneq ($(wildcard $(K)/src/cbank/cb_ccy.h),)
  DEFS += -DZXV_HAVE_CBANK_CCY
  INC  += -I$(K)/src/cbank -I$(K)/src/tensor
  KSRC += $(K)/src/cbank/cb_ccy.c $(K)/src/cbank/cb_ccy_tbl.c $(K)/src/cbank/cb_util.c
  ifneq ($(ZXV_NO_TENSOR),)
    KSRC += $(K)/src/tensor/zt.c
  endif
  # netting cycles (zxv_netting_*) on the cbank engine
  ifneq ($(wildcard $(K)/src/cbank/cb_net.h),)
    DEFS += -DZXV_HAVE_CBANK_NET
    KSRC += $(K)/src/cbank/cb_net.c $(K)/src/cbank/cb_config.c $(K)/src/cbank/cb_usury.c
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

# ABI 1.5 groups: each adds its own kernel sources and include paths on
# its own line, so the groups can be built side by side.
KSRC_TREATY := $(K)/src/treaty/zxt.c $(K)/src/treaty/zxt_exec.c $(K)/src/engine/zxv_engine.c \
               $(K)/src/abacus/abacus.c $(K)/src/pay/pay_ledger.c $(K)/src/pay/pay_util.c \
               $(K)/src/pay/pay_assure.c $(K)/src/swarm/swarm_market.c $(K)/src/tensor/zt.c
INC_TREATY  := -I$(K)/src/treaty -I$(K)/src/engine -I$(K)/src/pay -I$(K)/src/abacus \
               -I$(K)/src/zcapital -I$(K)/src/tensor
KSRC_AIPI := $(K)/src/aipi/aipi.c $(addprefix $(K)/src/ipfs_node/,ipfsn_multiformats.c \
             ipfsn_unixfs.c ipfsn_store.c ipfsn_net.c ipfsn_ubh.c) $(K)/src/tls/aead.c \
             $(K)/src/tls/hkdf.c $(K)/src/ubh/ubh.c $(K)/src/event_space/event_envelope.c
INC_AIPI  := -I$(K)/src/aipi -I$(K)/src/ipfs_node
KSRC_COIN := $(K)/src/vino_stores/vino_stores.c $(K)/src/onepolicy/onepolicy.c $(K)/src/edp_risk/edp_risk.c $(K)/src/swarm/swarm_denom.c
INC_COIN  := -I$(K)/src/vino_stores -I$(K)/src/onepolicy -I$(K)/src/edp_risk -I$(K)/src/surplus -I$(K)/src/swarm
KSRC_FEE := $(K)/src/pay/pay_assure.c $(K)/src/pay/pay_util.c $(K)/src/swarm/swarm_market.c $(K)/src/tensor/zt.c
INC_FEE  := -I$(K)/src/pay -I$(K)/src/swarm -I$(K)/src/zcapital -I$(K)/src/tensor
KSRC_VHUB := $(K)/src/vhub/vhub.c $(K)/src/pay/pay_ledger.c $(K)/src/pay/pay_util.c \
             $(K)/src/pay/pay_assure.c $(K)/src/swarm/swarm_market.c $(K)/src/tensor/zt.c
INC_VHUB  := -I$(K)/src/vhub -I$(K)/src/pay -I$(K)/src/tensor
KSRC_FREIGHT := $(K)/src/freight/freight.c $(K)/src/freight/freight_ops.c
INC_FREIGHT  := -I$(K)/src/freight
KSRC_WEB4 := $(if $(ZXV_NO_PQC),,$(addprefix $(K)/src/web4/,web4_agent.c web4_util.c web4_json.c) $(K)/src/pqsec/pq_mldsa65.c $(wildcard $(K)/src/pqsec/mldsa/*.c) $(K)/src/freight/freight.c $(K)/src/freight/freight_ops.c)
INC_WEB4  := $(if $(ZXV_NO_PQC),,-I$(K)/src/web4 -I$(K)/src/pqsec)
KSRC_PARADOX := $(K)/src/paradox/paradox.c $(K)/src/paradox/paradox_graph.c
INC_PARADOX  := -I$(K)/src/paradox
KSRC += $(KSRC_TREATY) $(KSRC_AIPI) $(KSRC_COIN) $(KSRC_FEE) $(KSRC_VHUB) $(KSRC_FREIGHT) $(KSRC_WEB4) $(KSRC_PARADOX)
INC  += $(INC_TREATY) $(INC_AIPI) $(INC_COIN) $(INC_FEE) $(INC_VHUB) $(INC_FREIGHT) $(INC_WEB4) $(INC_PARADOX)

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
	$(CC) $(SOFLAG) $(SAN) -o $@ $^ $(PTHREAD) -lm $(OSLIBS)

$(STATIC): $(OBJ) $(KOBJ)
	@rm -f $@
	$(AR) rcs $@ $^

# Header dependencies (kernel headers included), so an incremental build
# after a kernel merge never links stale objects.
-include $(OBJ:.o=.d) $(KOBJ:.o=.d)

TESTCC := $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Iinclude $(OSDEFS) $(SAN)
$(OUT)/test_zxv_api$(EXE): test/test_zxv_api.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(TESTLIBS) -o $@
$(OUT)/test_zxv_abi11$(EXE): test/test_zxv_abi11.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(TESTLIBS) -o $@
$(OUT)/test_zxv_abi12$(EXE): test/test_zxv_abi12.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(TESTLIBS) -o $@
$(OUT)/test_zxv_abi13$(EXE): test/test_zxv_abi13.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(TESTLIBS) -o $@
$(OUT)/test_zxv_abi14$(EXE): test/test_zxv_abi14.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(TESTLIBS) -o $@
$(OUT)/test_zxv_abi15_%$(EXE): test/test_zxv_abi15_%.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(TESTLIBS) -o $@
$(OUT)/test_zxv_abi16_%$(EXE): test/test_zxv_abi16_%.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(TESTLIBS) -o $@
# The portable (no __int128) Q16 rounding, forced with -DZXV_NO_INT128 and
# checked against the 128-bit path and the library.
$(OUT)/test_q16$(EXE): test/test_q16.c src/zxv_q16.h include/zxv_api.h $(LIB)
	$(TESTCC) -DZXV_NO_INT128 -Isrc $< -L$(OUT) -lzxv $(RPATH) $(TESTLIBS) -o $@
$(OUT)/gen_vectors$(EXE): tools/gen_vectors.c include/zxv_api.h $(LIB)
	$(TESTCC) $< -L$(OUT) -lzxv $(RPATH) $(TESTLIBS) -o $@
# a build-machine tool: it writes the fixtures, so a cross build runs it here
$(OUT)/mkfixture: tools/mkfixture.c
	@mkdir -p $(OUT)
	$(HOSTCC) -std=c11 -O1 -I$(K)/src/tensor -o $@ $<

test: $(OUT)/test_zxv_api$(EXE) $(OUT)/test_zxv_abi11$(EXE) $(OUT)/test_zxv_abi12$(EXE) $(OUT)/test_zxv_abi13$(EXE) \
      $(OUT)/test_zxv_abi14$(EXE) $(OUT)/test_q16$(EXE) fixtures \
      $(OUT)/test_zxv_abi15_treaty$(EXE) $(OUT)/test_zxv_abi15_aipi$(EXE) $(OUT)/test_zxv_abi15_coin$(EXE) $(OUT)/test_zxv_abi15_fee$(EXE) $(OUT)/test_zxv_abi15_vhub$(EXE) $(OUT)/test_zxv_abi15_freight$(EXE) $(OUT)/test_zxv_abi15_web4$(EXE) $(OUT)/test_zxv_abi15_paradox$(EXE) \
      $(OUT)/test_zxv_abi16_vino$(EXE)
	$(RUN) ./$(OUT)/test_zxv_api$(EXE)
	$(RUN) ./$(OUT)/test_zxv_abi11$(EXE) ../conformance/fixtures
	$(RUN) ./$(OUT)/test_zxv_abi12$(EXE) ../conformance/fixtures
	$(RUN) ./$(OUT)/test_zxv_abi13$(EXE)
	$(RUN) ./$(OUT)/test_zxv_abi14$(EXE)
	$(RUN) ./$(OUT)/test_q16$(EXE)
	$(RUN) ./$(OUT)/test_zxv_abi15_treaty$(EXE)
	$(RUN) ./$(OUT)/test_zxv_abi15_aipi$(EXE)
	$(RUN) ./$(OUT)/test_zxv_abi15_coin$(EXE)
	$(RUN) ./$(OUT)/test_zxv_abi15_fee$(EXE)
	$(RUN) ./$(OUT)/test_zxv_abi15_vhub$(EXE)
	$(RUN) ./$(OUT)/test_zxv_abi15_freight$(EXE)
	$(RUN) ./$(OUT)/test_zxv_abi15_web4$(EXE)
	$(RUN) ./$(OUT)/test_zxv_abi15_paradox$(EXE)
	$(RUN) ./$(OUT)/test_zxv_abi16_vino$(EXE)

fixtures: $(OUT)/mkfixture
	@mkdir -p ../conformance/fixtures
	./$(OUT)/mkfixture ../conformance/fixtures

vectors: $(OUT)/gen_vectors$(EXE) fixtures
	$(RUN) ./$(OUT)/gen_vectors$(EXE) ../conformance/fixtures > ../conformance/vectors.json

dist: all
	@rm -rf $(OUT)/dist && mkdir -p $(OUT)/dist/include $(OUT)/dist/lib
	cp include/zxv_api.h $(OUT)/dist/include/
	cp $(LIB) $(STATIC) $(wildcard $(OUT)/libzxv.dll.a) $(OUT)/dist/lib/

print-config:
	@echo "LIB=$(LIB)"; echo "DEFS=$(DEFS)"; echo "KSRC=$(KSRC)"

clean:
	rm -rf $(OUT)
