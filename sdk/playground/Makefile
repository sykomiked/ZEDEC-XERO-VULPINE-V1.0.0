# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/playground - templates and one runner for every ZXV language, the
# `zxv` command (zxv list / new / run / test) and the Playground tab's engine.
#
#   make              build/libzxvplay.a and build/zxv
#   make test         run every template and every test case against the goldens
#   make golden       rewrite test/golden/*.json from the current runner
#   make check-nofloat
#   make clean
#
# Options: SANITIZE=1, OUT=dir (default build/).
#
# Depends on libzxv (sdk/libzxv), the five pillars (sdk/pillars/*) and the
# treaty runner in libzxv-econ (packages/); `make` builds them first.

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
OUT     ?= build
ROOT    := $(abspath ../..)
ZXV     := $(ROOT)/sdk/libzxv
PIL     := $(ROOT)/sdk/pillars
KSRC    := $(ROOT)/kernel/src
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
endif

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif

WARN := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
INC  := -Iinclude -Isrc -I$(ZXV)/include \
        -I$(PIL)/cobol-sutra/include -I$(PIL)/fortran-trebago/include -I$(PIL)/ada-eve/include \
        -I$(PIL)/apl-kron/include -I$(PIL)/mumps-myra/include \
        -I$(ROOT)/kernel/include -I$(KSRC)/treaty -I$(KSRC)/engine -I$(KSRC)/pay -I$(KSRC)/abacus \
        -I$(KSRC)/rational -I$(KSRC)/swarm -I$(KSRC)/tensor -I$(KSRC)/mlkem -I$(KSRC)/zcapital \
        -I$(KSRC)/modbind -I$(KSRC)/e8 -I$(KSRC)/event_space -I$(KSRC)/surplus
CFLAGS_ := -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(INC)

SRC  := src/zp_core.c src/zp_treaty.c src/zp_sutra.c src/zp_trebago.c src/zp_eve.c src/zp_kron.c \
        src/zp_myra.c
OBJ  := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRC)) $(OUT)/obj/zp_templates.o
LIB  := $(OUT)/libzxvplay.a
TEMPLATES := $(sort $(wildcard templates/*/*))

PILLAR_LIBS := $(PIL)/cobol-sutra/build/libzsutra.a $(PIL)/fortran-trebago/build/libztrebago.a \
               $(PIL)/ada-eve/build/libzeve.a $(PIL)/apl-kron/build/libzkron.a \
               $(PIL)/mumps-myra/build/libzmyra.a
ECON_LIB := $(ROOT)/packages/build/libzxv-econ/libzxv-econ.a

LINK := $(LIB) $(PILLAR_LIBS) $(ECON_LIB) -L$(ZXV)/build -lzxv -Wl,-rpath,$(ZXV)/build -lpthread $(SAN)

.PHONY: all deps test golden check-nofloat clean

all: $(LIB) $(OUT)/zxv

deps:
	$(MAKE) -C $(ZXV) all
	for p in cobol-sutra fortran-trebago ada-eve apl-kron mumps-myra; do $(MAKE) -C $(PIL)/$$p all || exit 1; done
	$(MAKE) -C $(ROOT)/packages econ

$(ZXV_LIB) $(PILLAR_LIBS) $(ECON_LIB):
	$(MAKE) deps

$(OUT)/zp_templates.c: tools/gen_templates.py $(TEMPLATES)
	@mkdir -p $(OUT)
	$(PYTHON) tools/gen_templates.py templates $@

$(OUT)/obj/zp_templates.o: $(OUT)/zp_templates.c src/zp_internal.h include/zxvplay.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(OUT)/obj/%.o: src/%.c src/zp_internal.h include/zxvplay.h | $(ZXV_LIB) $(PILLAR_LIBS) $(ECON_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/zxv: tools/zxv.c $(LIB) $(PILLAR_LIBS) $(ECON_LIB)
	$(CC) -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -g $(WARN) $(SAN) $(INC) $< $(LINK) -o $@

$(OUT)/test_play: test/test_play.c $(LIB) $(PILLAR_LIBS) $(ECON_LIB)
	$(CC) -std=c11 -D_DEFAULT_SOURCE -O1 -g $(WARN) $(SAN) $(INC) $< $(LINK) -o $@

test: check-nofloat $(OUT)/test_play $(OUT)/zxv
	$(OUT)/test_play templates test/golden test/cases
	sh test/test_cli.sh $(OUT)/zxv

golden: $(OUT)/test_play
	$(OUT)/test_play --update templates test/golden test/cases

# R11: no float types in the runner (the pillars check their own).
check-nofloat:
	@if grep -nE '\b(float|double|long double)\b' src/*.c src/*.h include/*.h tools/zxv.c; then \
	  echo "[FAIL] float type in sdk/playground"; exit 1; fi
	@echo "[PASS] sdk/playground has no float types"

clean:
	rm -rf $(OUT)
