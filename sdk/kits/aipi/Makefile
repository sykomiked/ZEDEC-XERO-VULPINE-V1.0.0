# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/kits/aipi — AIPI on Z-Wasm: content-addressed functions run in the
# Z-Wasm sandbox inside their Z-Contract boundary.
#
#   make                 build/libaipi_zwasm.a (+ the kernel AIPI core)
#   make test            end-to-end tests over real Wasm modules, and the
#                        no-float check
#   make demo            the walk-through in examples/aipi_demo.c
#   make clean
#
# Options:
#   SANITIZE=1           -fsanitize=address,undefined for the kit, z-wasm,
#                        z-contract (rebuilt under $(OUT)) and the tests
#
# The AIPI core is the kernel's own src/aipi (freestanding C), compiled here
# for the host together with the parts of src/ipfs_node and src/lpres it
# uses. Test modules are assembled in C (z-wasm's test/wasm_build.h), so the
# tests need no Wasm toolchain.

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../../libzxv
ZW      := ../z-wasm
ZC      := ../z-contract
K       := ../../../kernel
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
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
ZCW_LIB := $(ZC_OUT)/libzcontract_wasm.a

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
KINC    := -I$(K)/src/aipi -I$(K)/include -I$(K)/src/lpres -I$(K)/src/event_space \
           -I$(K)/src/modbind -I$(K)/src/surplus
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) \
           -Iinclude $(KINC) -I$(ZXV)/include -I$(ZW)/include -I$(ZC)/include
# The kernel sources keep the kernel's own warning set.
KFLAGS  := -std=c11 -O2 -g -fPIC -Wall -Wextra -Werror $(SAN) -DTEST_HOST $(KINC)

KSRC := $(K)/src/aipi/aipi.c $(K)/src/ipfs_node/ipfsn_multiformats.c \
        $(K)/src/ipfs_node/ipfsn_unixfs.c $(K)/src/ipfs_node/ipfsn_store.c \
        $(K)/src/ipfs_node/ipfsn_net.c $(K)/src/ipfs_node/ipfsn_ubh.c \
        $(K)/src/ipfs_node/ipfsn_fidx.c $(K)/src/robin_debanks/sha256.c $(K)/src/tls/aead.c \
        $(K)/src/tls/hkdf.c $(K)/src/ubh/ubh.c $(K)/src/event_space/event_envelope.c \
        $(K)/src/lpres/lpres_hex.c $(K)/src/lpres/lpres.c
KOBJ := $(patsubst $(K)/src/%.c,$(OUT)/kobj/%.o,$(KSRC))
KLIB := $(OUT)/libaipi_core.a
LIB  := $(OUT)/libaipi_zwasm.a
HDRS := include/aipi_zwasm.h $(K)/src/aipi/aipi.h $(ZW)/include/zwasm.h $(ZC)/include/zcontract.h

LINK   := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -lm $(SAN)
TESTCC = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) -Iinclude $(KINC) -I$(ZXV)/include \
         -I$(ZW)/include -I$(ZW)/test -I$(ZC)/include -Itest

.PHONY: all test demo check-nofloat clean

all: $(LIB) $(KLIB)

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(ZW_LIB): $(wildcard $(ZW)/src/*.c $(ZW)/src/*.h $(ZW)/include/*.h)
	$(MAKE) -C $(ZW) CC=$(CC) SANITIZE=$(SANITIZE) OUT=$(ZW_OUT) $(ZW_LIB)

$(ZC_LIB) $(ZCW_LIB): $(wildcard $(ZC)/src/*.c $(ZC)/src/*.h $(ZC)/include/*.h)
	$(MAKE) -C $(ZC) CC=$(CC) SANITIZE=$(SANITIZE) OUT=$(ZC_OUT) $(ZC_LIB) $(ZCW_LIB)

$(OUT)/kobj/%.o: $(K)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KFLAGS) -c $< -o $@ 2>$@.log || { cat $@.log; exit 1; }

$(KLIB): $(KOBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/obj/aipi_zwasm.o: src/aipi_zwasm.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OUT)/obj/aipi_zwasm.o
	@rm -f $@
	$(AR) rcs $@ $^

DEPLIBS := $(LIB) $(KLIB) $(ZCW_LIB) $(ZC_LIB) $(ZW_LIB)

$(OUT)/test_aipi_zwasm: test/test_aipi_zwasm.c test/aipi_mods.h $(DEPLIBS) $(ZXV_LIB)
	$(TESTCC) $< $(DEPLIBS) $(LINK) -o $@

$(OUT)/aipi_demo: examples/aipi_demo.c test/aipi_mods.h $(DEPLIBS) $(ZXV_LIB)
	$(TESTCC) $< $(DEPLIBS) $(LINK) -o $@

test: check-nofloat $(OUT)/test_aipi_zwasm
	$(OUT)/test_aipi_zwasm

demo: $(OUT)/aipi_demo
	$(OUT)/aipi_demo

# R11: no float type in the kit's own sources (z-wasm's checker).
check-nofloat:
	$(PYTHON) $(ZW)/tools/check_nofloat.py src/aipi_zwasm.c include/aipi_zwasm.h \
	  $(K)/src/aipi/aipi.c $(K)/src/aipi/aipi.h

clean:
	rm -rf $(OUT)
