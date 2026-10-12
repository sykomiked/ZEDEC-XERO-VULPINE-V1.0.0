# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/embedded/baremetal — ZXV-BareMetal: board manifests and boot packaging
# for the ZXV kernel on x86_64 and arm64 UEFI (on top of the PR #25 disc
# build in build_system/), plus QEMU-only device-tree targets.
#
#   make test           everything that needs no Docker, QEMU or cross compiler:
#                       validator unit tests, example manifests, dry-run plans,
#                       the R1 profile against libzxv, the no-float check
#   make validate       validate boards/*.json
#   make plans          dry-run the packaging plan of every example board
#   make check-profile  profiles/r1.zhp is still libzxv's reference R1, and every
#                       example board pins its digest
#   make smoke          OPTIONAL: really build the x86_64 disc on this machine
#                       and boot it under OVMF (needs the PR #25 host toolchain,
#                       qemu-system-x86_64 and OVMF); not part of `test`
#   make clean

ifeq ($(origin CC),default)
  CC := gcc
endif
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../../libzxv
REPO    := ../../..
NOFLOAT_PY := ../../kits/z-wasm/tools/check_nofloat.py

ifeq ($(shell uname -s),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
endif
ZXV_ABS := $(abspath $(ZXV)/build)
ZHP     := $(OUT)/zhp_tool
BOARD   := $(PYTHON) tools/zxv_board.py
BOARDS  := $(sort $(wildcard boards/*.json))

.PHONY: all test unit validate plans check-profile check-nofloat smoke clean

all: $(ZHP)

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(ZHP): tools/zhp_tool.c $(ZXV)/include/zxv_api.h $(ZXV_LIB)
	@mkdir -p $(OUT)
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror -I$(ZXV)/include $< -L$(ZXV_ABS) -lzxv \
	  -Wl,-rpath,$(ZXV_ABS) -o $@

unit:
	$(PYTHON) test/test_zxv_board.py

validate:
	$(BOARD) validate $(BOARDS)

# Every example board must give a plan; riscv64 is host-only by design.
plans:
	@set -e; for b in $(BOARDS); do \
	  if grep -q '"riscv64"' $$b; then $(BOARD) package $$b --dry-run --host; \
	  else $(BOARD) package $$b --dry-run; \
	    if grep -q '"uefi"' $$b; then $(BOARD) package $$b --dry-run --host --boot-check; fi; fi; \
	done
	@echo "plans: $(words $(BOARDS)) boards planned, nothing written"

check-profile: $(ZHP)
	$(ZHP) reference $(OUT)/r1.zhp > $(OUT)/r1.digest
	cmp $(OUT)/r1.zhp profiles/r1.zhp
	$(ZHP) digest profiles/r1.zhp > $(OUT)/r1.decoded
	cmp $(OUT)/r1.digest $(OUT)/r1.decoded
	@d=$$(cat $(OUT)/r1.digest); for b in $(BOARDS); do \
	  grep -q "\"digest\": \"$$d\"" $$b || { echo "$$b does not pin R1 $$d"; exit 1; }; done
	@echo "check-profile: profiles/r1.zhp = libzxv reference R1 ($$(cat $(OUT)/r1.digest)), decoded canonically"

check-nofloat:
	$(PYTHON) $(NOFLOAT_PY) tools/zhp_tool.c

test: check-nofloat unit validate plans check-profile
	@echo "baremetal: test passed"

SMOKE_OUT := $(abspath $(OUT))/smoke
smoke:
	$(BOARD) package boards/qemu-x86-64-ovmf.json --host --boot-check --out $(SMOKE_OUT)
	cd $(SMOKE_OUT) && sha256sum -c SHA256SUMS

clean:
	rm -rf $(OUT)
