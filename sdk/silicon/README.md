<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV for hardware designers (`sdk/silicon`)

This area is for people who put ZXV's rules into logic: FPGA and ASIC
designers, and anyone building a ZHP-1 hardware component (see
[HW_PROFILE.md](../docs/HW_PROFILE.md)) that serves a ZXV function in
hardware.

Everything here is **generated from libzxv**, never written twice: a small
C program asks the real library for its answers through the public ABI
([`zxv_api.h`](../libzxv/include/zxv_api.h)), and the HDL is produced from
those answers and tested against them. The same kit rules apply as in
[`sdk/kits`](../kits/README.md): public ABI only, no floats, honest limits,
its own version.

| kit | folder | what it is | status | tests |
|---|---|---|---|---|
| Z-Fabric | [`z-fabric`](z-fabric/) | Four-valued NOT/AND/OR, the 64-entry hexagram action table, phase cycle and tag combinators as gate-level Verilog-2005 and VHDL-2008. A freshness check fails CI if the HDL drifts from libzxv. | kit 0.1 (preview) | 8,368 exhaustive vectors under Icarus Verilog and GHDL, a Python netlist simulator on both, post-synthesis simulation (Yosys, GHDL), mutation check |

## Limits that apply to everything here

Combinational reference logic only. Nothing is timing-closed for any FPGA
or ASIC, nothing has been taped out or run on a physical device, and no
safety or security certification is claimed. Each kit README has the
details.

CI: [`.github/workflows/sdk-silicon.yml`](../../.github/workflows/sdk-silicon.yml).
