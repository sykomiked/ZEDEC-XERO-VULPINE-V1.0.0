# ZXV hardware

Chip, board, chassis and reconfigurable-hardware designs for ZXV.

## Licence in plain words

These designs are **source-available** under the
[ZXV Hardware License 1.1](LICENSE). They are not open source and are not
under the Apache License that covers the ZXV software.

- **Free for noncommercial use.** Study, build, modify and share them for
  personal, educational, research or charitable purposes at no cost.
- **Anyone may evaluate.** Companies may study them and build up to 10
  prototypes for internal evaluation without charge.
- **Commercial manufacture or sale** needs the Commercial Licence Agreement
  and a royalty of 18% of gross revenue, paid directly to ILLUMAHEART
  FOUNDATION. Manufacturers partner with 36N9 GENETICS LLC, the root
  organisation that originated these designs, under its master partnership
  licence. Approach it only through one of its two pre-approved partners,
  ILLUMAHEART FOUNDATION or SCENIC BYPASS CONSULTING, LLC, never 36N9
  GENETICS LLC directly.
- **No royalty for public infrastructure.** Water, power, health, emergency
  services, transport, communications, education and similar public-good
  projects are exempt once registered (Section 4A).
- **Military operations are not licensed**, except rescue and life-saving
  missions, and defence of a nation against external armed attack or armed
  insurgency, within the laws of war (Sections 4A.6 to 4A.10). Armed forces
  may use the hardware freely for non-military purposes.
- **Credit is required** everywhere the designs or products are used. See
  [NOTICE](NOTICE) for the exact wording.
- **No patents.** The designs are published as prior art so nobody can
  patent them. See [PRIOR_ART.md](PRIOR_ART.md).

The full licence text governs; this summary does not replace it.

## Layout

| Folder | Contents |
|---|---|
| `disclosures/` | Written technical disclosures (prior art) |
| `rtl/` | Verilog, SystemVerilog, VHDL |
| `silicon/` | Physical design, layout scripts |
| `boards/` | Schematics and PCB layouts |
| `chassis/` | Mechanical drawings and CAD |
| `config/` | Configuration Files: bitstreams, microcode, emulation profiles |
| `docs/` | Design documentation |

Every file here carries `SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1`
(or is covered by [`REUSE.toml`](../REUSE.toml)). Firmware that runs on ZXV
hardware lives in `kernel/` and `sdk/` under Apache-2.0.
