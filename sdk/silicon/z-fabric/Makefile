# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/silicon/z-fabric — libzxv's four-valued logic and 64-state hexagram
# tables as synthesizable Verilog-2005 and VHDL-2008, generated from the C
# library itself.
#
#   make gen            dump the tables from libzxv, regenerate tables/, hdl/, tb/
#   make check-fresh    regenerate into build/fresh and diff with the committed
#                       files: fails if the HDL drifted from libzxv
#   make sim-verilog    exhaustive testbench under Icarus Verilog (iverilog)
#   make sim-vhdl       exhaustive testbench under GHDL (VHDL-2008)
#   make netsim         the Python reference netlist simulator on both HDL files
#   make synth          Yosys synthesis of every module (check -assert), then the
#                       Verilog testbench on the synthesized netlist
#   make synth-vhdl     GHDL synthesis of every entity, then the VHDL testbench
#                       on the synthesized netlist
#   make mutant         a corrupted vector must make every simulator fail
#   make test-gen       generator unit tests
#   make check-nofloat  R11 on the C dumper
#   make check          all of the above
#   make clean
#
# A missing simulator or synthesizer is an error; ZFAB_ALLOW_MISSING=1 turns
# it into a printed SKIPPED line (netsim, check-fresh and test-gen always run).

ifeq ($(origin CC),default)
  CC := gcc
endif
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../../libzxv
NOFLOAT_PY := ../../kits/z-wasm/tools/check_nofloat.py

ifeq ($(shell uname -s),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
endif
ZXV_ABS := $(abspath $(ZXV)/build)
OUT_ABS := $(abspath $(OUT))

DUMP    := $(OUT)/zfab_dump
GEN     := $(PYTHON) gen/zfab_gen.py
HDL_V   := hdl/verilog/zxv_fabric.v
HDL_VHD := hdl/vhdl/zxv_fabric.vhd
TB_V    := tb/verilog/tb_zxv_fabric.v
TB_VHD  := tb/vhdl/tb_zxv_fabric.vhd
GHDLFLAGS := --std=08

define need
	@command -v $(1) >/dev/null 2>&1 || { \
	  if [ -n "$(ZFAB_ALLOW_MISSING)" ]; then echo "SKIPPED $@: $(1) not installed"; exit 0; fi; \
	  echo "$@: $(1) not installed (apt-get install $(2); or ZFAB_ALLOW_MISSING=1)"; exit 1; }
endef

.PHONY: all gen check check-fresh sim-verilog sim-vhdl netsim synth synth-vhdl mutant \
        test-gen check-nofloat clean

all: $(DUMP)

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(DUMP): tools/zfab_dump.c $(ZXV)/include/zxv_api.h $(ZXV_LIB)
	@mkdir -p $(OUT)
	$(CC) -std=c11 -O1 -Wall -Wextra -Werror -I$(ZXV)/include $< -L$(ZXV_ABS) -lzxv \
	  -Wl,-rpath,$(ZXV_ABS) -o $@

gen: $(DUMP)
	$(DUMP) > tables/zxv_tables.json.tmp
	mv tables/zxv_tables.json.tmp tables/zxv_tables.json
	$(GEN) --tables tables/zxv_tables.json --out .

check-fresh: $(DUMP)
	rm -rf $(OUT)/fresh
	mkdir -p $(OUT)/fresh/tables
	$(DUMP) > $(OUT)/fresh/tables/zxv_tables.json
	$(GEN) --tables $(OUT)/fresh/tables/zxv_tables.json --out $(OUT)/fresh
	diff -ru tables $(OUT)/fresh/tables
	diff -ru hdl $(OUT)/fresh/hdl
	diff -ru tb $(OUT)/fresh/tb
	@echo "check-fresh: tables, HDL and testbenches match libzxv"

# run_tb <dir> <vectors>: the simulators read vectors/ relative to the cwd.
$(OUT)/sim/vectors: $(wildcard tb/vectors/*.hex)
	rm -rf $(OUT)/sim
	mkdir -p $(OUT)/sim
	cp -r tb/vectors $(OUT)/sim/vectors

sim-verilog: $(OUT)/sim/vectors
	$(call need,iverilog,iverilog)
	@if command -v iverilog >/dev/null 2>&1; then set -e; \
	  iverilog -g2005 -Wall -o $(OUT)/sim/tb_v.vvp $(HDL_V) $(TB_V); \
	  ( cd $(OUT)/sim && vvp -n tb_v.vvp ) | tee $(OUT)/sim/verilog.log; \
	  grep -q '^ALL PASS' $(OUT)/sim/verilog.log; fi

sim-vhdl: $(OUT)/sim/vectors
	$(call need,ghdl,ghdl)
	@if command -v ghdl >/dev/null 2>&1; then set -e; \
	  mkdir -p $(OUT)/sim/work; \
	  ghdl -a $(GHDLFLAGS) --workdir=$(OUT)/sim/work $(HDL_VHD) $(TB_VHD); \
	  ( cd $(OUT)/sim && ghdl -r $(GHDLFLAGS) --workdir=work tb_zxv_fabric ) | tee $(OUT)/sim/vhdl.log; \
	  grep -q 'ALL PASS' $(OUT)/sim/vhdl.log; fi

netsim:
	$(PYTHON) tools/zfab_netsim.py --vectors tb/vectors $(HDL_V) $(HDL_VHD)

# Yosys: generic synthesis of every module; check -assert fails on latches,
# undriven or multiply driven nets. The synthesized netlist is then run
# through the same exhaustive testbench (post-synthesis simulation).
synth: $(OUT)/sim/vectors
	$(call need,yosys,yosys)
	@if command -v yosys >/dev/null 2>&1; then set -e; \
	  mkdir -p $(OUT)/synth; \
	  yosys -q -l $(OUT)/synth/yosys.log -p "read_verilog $(HDL_V); synth; check -assert; \
	    tee -o $(OUT)/synth/stat.txt stat; write_verilog -noattr $(OUT)/synth/zxv_fabric_yosys.v"; \
	  ! grep -qi 'latch' $(OUT)/synth/stat.txt; \
	  grep -E "^=== |Number of cells" $(OUT)/synth/stat.txt | paste - - | \
	    awk '{print "synth " $$2 ": " $$NF " cells"}'; \
	  command -v iverilog >/dev/null 2>&1 || { echo "SKIPPED post-synthesis sim: no iverilog"; exit 0; }; \
	  iverilog -g2005 -o $(OUT)/sim/tb_synth.vvp $(OUT)/synth/zxv_fabric_yosys.v $(TB_V); \
	  ( cd $(OUT)/sim && vvp -n tb_synth.vvp ) > $(OUT)/sim/synth.log; \
	  grep '^ALL PASS' $(OUT)/sim/synth.log | sed 's/^/post-synthesis (yosys) /'; \
	  grep -q '^ALL PASS' $(OUT)/sim/synth.log; fi

synth-vhdl: $(OUT)/sim/vectors
	$(call need,ghdl,ghdl)
	@if command -v ghdl >/dev/null 2>&1; then set -e; \
	  mkdir -p $(OUT)/synth/ghdl $(OUT)/sim/swork; \
	  ghdl -a $(GHDLFLAGS) --workdir=$(OUT)/synth/ghdl $(HDL_VHD); \
	  : > $(OUT)/synth/zxv_fabric_ghdl.vhd; \
	  for e in $$(sed -n 's/^entity \([a-z0-9_]*\) is$$/\1/p' $(HDL_VHD)); do \
	    ghdl --synth $(GHDLFLAGS) --workdir=$(OUT)/synth/ghdl $$e >> $(OUT)/synth/zxv_fabric_ghdl.vhd; \
	  done; \
	  echo "synth-vhdl: $$(grep -c '^entity' $(OUT)/synth/zxv_fabric_ghdl.vhd) entities synthesized by GHDL"; \
	  ghdl -a $(GHDLFLAGS) --workdir=$(OUT)/sim/swork $(OUT)/synth/zxv_fabric_ghdl.vhd $(TB_VHD); \
	  ( cd $(OUT)/sim && ghdl -r $(GHDLFLAGS) --workdir=swork tb_zxv_fabric ) > $(OUT)/sim/synth-vhdl.log 2>&1; \
	  grep 'ALL PASS' $(OUT)/sim/synth-vhdl.log | sed 's/.*ALL PASS/post-synthesis (ghdl) ALL PASS/'; \
	  grep -q 'ALL PASS' $(OUT)/sim/synth-vhdl.log; fi

# One expected value changed (hex_action of tag 0: refuse -> proceed): the
# Verilog and VHDL testbenches and netsim must all reject it.
mutant:
	rm -rf $(OUT)/mutant
	mkdir -p $(OUT)/mutant/work
	cp -r tb/vectors $(OUT)/mutant/vectors
	$(PYTHON) -c "import sys; p=sys.argv[1]; l=open(p).read().split(); \
	  l[0]='%x' % ((int(l[0],16)+1) % 5); open(p,'w').write('\n'.join(l)+'\n')" \
	  $(OUT)/mutant/vectors/zxv_hex_action.hex
	@if $(PYTHON) tools/zfab_netsim.py --vectors $(OUT)/mutant/vectors $(HDL_V) > $(OUT)/mutant/netsim.log; \
	  then echo "mutant: netsim missed the corrupted vector"; exit 1; \
	  else echo "mutant: netsim rejects it ($$(grep -c '^FAIL ' $(OUT)/mutant/netsim.log) failing vector)"; fi
	@if command -v iverilog >/dev/null 2>&1; then \
	  iverilog -g2005 -o $(OUT)/mutant/tb_v.vvp $(HDL_V) $(TB_V) && \
	  if ( cd $(OUT)/mutant && vvp -n tb_v.vvp ) > $(OUT)/mutant/verilog.log 2>&1; \
	  then echo "mutant: iverilog testbench passed a corrupted vector"; exit 1; \
	  else echo "mutant: iverilog testbench rejects it (exit non-zero)"; fi; \
	  else echo "SKIPPED mutant iverilog: not installed"; fi
	@if command -v ghdl >/dev/null 2>&1; then \
	  ghdl -a $(GHDLFLAGS) --workdir=$(OUT)/mutant/work $(HDL_VHD) $(TB_VHD) && \
	  if ( cd $(OUT)/mutant && ghdl -r $(GHDLFLAGS) --workdir=work tb_zxv_fabric ) > $(OUT)/mutant/vhdl.log 2>&1; \
	  then echo "mutant: GHDL testbench passed a corrupted vector"; exit 1; \
	  else echo "mutant: GHDL testbench rejects it (exit non-zero)"; fi; \
	  else echo "SKIPPED mutant ghdl: not installed"; fi

test-gen:
	$(PYTHON) test/test_zfab_gen.py

check-nofloat:
	$(PYTHON) $(NOFLOAT_PY) tools/zfab_dump.c

check: check-nofloat test-gen check-fresh netsim sim-verilog sim-vhdl synth synth-vhdl mutant
	@echo "z-fabric: check passed"

clean:
	rm -rf $(OUT)
