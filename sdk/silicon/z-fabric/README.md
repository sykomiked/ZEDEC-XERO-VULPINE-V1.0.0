<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Z-Fabric: libzxv's logic tables as HDL

Z-Fabric turns two parts of libzxv into synthesizable combinational logic,
in **Verilog-2005** and **VHDL-2008**:

- the **four-valued logic** NOT, AND and OR on `ZXV_LP_*` codes
  (`zxv_lp_not`, `zxv_lp_and`, `zxv_lp_or`), plus the `ZXV_LP_*` /
  `ZXV_HEX_BASE_*` code converters;
- the **64-state hexagram tag**: the 64-entry action table
  (`zxv_hex_action`), the phase cycle (`zxv_hex_phase_next`) and the tag
  combinators `zxv_hex_not`, `zxv_hex_and`, `zxv_hex_or`.

Kit version 0.1 (preview).

## Generated from the C library, so it cannot drift

```
libzxv (C)  --tools/zfab_dump.c-->  tables/zxv_tables.json  --gen/zfab_gen.py-->  hdl/ + tb/
```

1. [`tools/zfab_dump.c`](tools/zfab_dump.c) links the real libzxv and asks
   it every input of every function through the public ABI
   (`zxv_api.h`), one call per entry: 8,368 entries. It also checks that
   `zxv_hex_pack`/`zxv_hex_unpack` use the bit layout the HDL assumes
   (`tag = base << 4 | polarity << 2 | phase`). Any non-OK status fails.
2. [`gen/zfab_gen.py`](gen/zfab_gen.py) (Python 3, standard library only)
   reads that JSON. For each output bit it finds the input bits it depends
   on, minimises a sum of products (Quine-McCluskey with a deterministic
   cover), and evaluates the equation on every input against the table
   before writing anything. It emits gates (`&`, `|`, `~` / `and`, `or`,
   `not`) only: no ROMs, no `case`, no latches, no clocks.
3. `make check-fresh` rebuilds the dumper against the current libzxv,
   regenerates everything into `build/fresh` and diffs it with the
   committed `tables/`, `hdl/` and `tb/`. If anyone changes the C semantics
   without regenerating, this fails in CI.

The committed files are outputs: never edit them by hand, run `make gen`.

## Modules

Inputs are listed most significant first; the testbench index is their
concatenation.

| module / entity | ports | libzxv function |
|---|---|---|
| `zxv_lp_not` | `a[1:0]` -> `y[1:0]` | `zxv_lp_not` |
| `zxv_lp_and`, `zxv_lp_or` | `a[1:0]`, `b[1:0]` -> `y[1:0]` | `zxv_lp_and`, `zxv_lp_or` |
| `zxv_hex_from_lp` | `lp[1:0]` -> `base[1:0]` | `zxv_hex_from_lp` |
| `zxv_hex_to_lp` | `base[1:0]` -> `lp[1:0]` | `zxv_hex_to_lp` |
| `zxv_hex_phase_next` | `phase[1:0]` -> `next_phase[1:0]` | `zxv_hex_phase_next` |
| `zxv_hex_action` | `tag[5:0]` -> `action[2:0]` (`ZXV_HEX_ACT_*`, 0..4) | `zxv_hex_action` |
| `zxv_hex_not` | `a[5:0]` -> `y[5:0]` | `zxv_hex_not` |
| `zxv_hex_and`, `zxv_hex_or` | `a[5:0]`, `b[5:0]` -> `y[5:0]` | `zxv_hex_and`, `zxv_hex_or` |

Codes are libzxv's, unchanged. Note that `ZXV_LP_*` (NEITHER 0, TRUE 1,
FALSE 2, BOTH 3) and `ZXV_HEX_BASE_*` (FALSE 0, NEITHER 1, BOTH 2, TRUE 3)
are numbered differently, exactly as in C; convert with the two converter
modules, never by wiring.

Every input of a 2-bit or 6-bit port is a valid value, so the hardware has
no `ZXV_E_ARG` path: the C library's argument errors (a tag above 63, a
NULL pointer) cannot be expressed on these ports.

Synthesis, Yosys 0.33 generic `synth` (gate cells after optimisation):
`zxv_hex_and` 20, `zxv_hex_or` 18, `zxv_hex_action` 16, `zxv_lp_and` 8,
`zxv_lp_or` 6, `zxv_hex_not` 2, the converters and phase cycle 1 each,
`zxv_lp_not` 0 (wires). 57 product terms in all.

## Tests

`make check` runs all of it; CI runs it in
[`sdk-silicon.yml`](../../../.github/workflows/sdk-silicon.yml) on
ubuntu-24.04 with `apt-get install iverilog ghdl yosys`.

| target | what it checks |
|---|---|
| `check-fresh` | regenerated tables, HDL and testbenches equal the committed ones |
| `sim-verilog` | Icarus Verilog (`-g2005`): exhaustive self-checking testbench, 8,368 vectors over 10 modules, `!==` so X/Z fail |
| `sim-vhdl` | GHDL (`--std=08`): the same 8,368 vectors; a short or long vector file fails |
| `netsim` | [`tools/zfab_netsim.py`](tools/zfab_netsim.py), an independent Python simulator that parses the generated Verilog **and** VHDL (gates only; anything else is an error) and checks every vector: 16,736 |
| `synth` | Yosys `synth` + `check -assert` on every module, no latches, then the Verilog testbench on the **synthesized** netlist |
| `synth-vhdl` | `ghdl --synth` of every entity, then the VHDL testbench on the synthesized netlist |
| `mutant` | one expected value corrupted: netsim, the iverilog and the GHDL testbenches must each fail |
| `test-gen` | generator and netsim unit tests: every 1-, 2- and 3-input function, 300 random tables up to 7 inputs, loader rejections, malformed netlists |
| `check-nofloat` | R11 on the C dumper |

Without a simulator installed, `make check ZFAB_ALLOW_MISSING=1` prints
`SKIPPED` for that step; `netsim`, `check-fresh` and `test-gen` always run.

## Limits

- **Combinational reference only.** These are truth-table-exact gate
  equations, minimised for readability, not for area, delay or power. No
  registers, pipelining, reset or clocking.
- **Not timing-closed for any FPGA or ASIC.** No constraints, no
  place-and-route, no static timing analysis, no target library. Yosys and
  GHDL synthesis here are generic (technology-independent) checks that the
  code is synthesizable and still correct after synthesis.
- **No silicon.** Nothing has been taped out, fabricated or run on an FPGA
  board. No claim about any physical device.
- **Only these functions.** The rest of libzxv (rationals, rmag budgets,
  the phase clock, the quarantine log, cryptography) is not here.
  `zxv_hex_step`, `zxv_hex_set`, `zxv_hex_forbid` and `zxv_hex_polarity`
  are not generated yet.
- **Exhaustive, not formal.** Correctness is shown by checking every input
  against libzxv's own answers (the input spaces are at most 12 bits),
  three ways plus post-synthesis. There is no formal equivalence proof
  against the C source, and the tests are only as right as libzxv is.

## Files

| path | |
|---|---|
| `tools/zfab_dump.c` | dumps the tables from libzxv (public ABI only) |
| `gen/zfab_gen.py` | the generator |
| `tables/zxv_tables.json` | the dumped tables (generated, committed) |
| `hdl/verilog/zxv_fabric.v`, `hdl/vhdl/zxv_fabric.vhd` | the HDL (generated, committed) |
| `tb/` | testbenches and vector files (generated, committed) |
| `tools/zfab_netsim.py` | the reference netlist simulator |
| `test/test_zfab_gen.py` | unit tests |
