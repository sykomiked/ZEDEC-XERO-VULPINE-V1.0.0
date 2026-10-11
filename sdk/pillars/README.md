<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Legacy-language pillars

A pillar brings one legacy language's data into ZXV without changing the
language. Every pillar follows the same pattern:

1. **The legacy format is the treaty.** The pillar reads the language's
   own record or data description (for COBOL, the copybook) and does not
   ask anyone to rewrite it.
2. **A shorthand compiles to the same thing.** Each pillar has a compact
   syntax that declares the same layouts. Both forms compile to one
   canonical layout, so equivalence is a digest comparison.
3. **A parser in C over libzxv.** The parser and the value codec are
   plain C that calls only the [libzxv ABI](../docs/ABI.md). They run in
   userland, in your process.
4. **Exact results only.** Values are signed integers with an implied
   scale, or libzxv exact rationals. There are no floats (R11). A value
   that does not fit is refused, never rounded.
5. **State changes go through capabilities.** A record that moves value
   becomes a libzxv ledger posting through a `*_cap` call with a
   capability the caller passes. Balancing is the ledger's job, and its
   refusals come back as status codes.

| Pillar | Language | Shorthand | Folder | Tests |
|---|---|---|---|---|
| cobol-sutra | COBOL copybooks (data division subset) | Sutra | [`cobol-sutra`](cobol-sutra/) | 154,569 checks, golden records from GnuCOBOL, fuzzed under ASan + UBSan |
| fortran-trebago | Fortran 2008+ (module `zxv`, ISO_C_BINDING) | Trebago (exact numeric kernels) | [`fortran-trebago`](fortran-trebago/) | 75,179 checks in C and Fortran, 26 golden cases, 14 compile-fail cases, fuzzed under ASan + UBSan |
| ada-eve | Ada 2012 (package `Zxv`, Interfaces.C; `Zxv.Fixed` in SPARK) | Eve (compact type declarations) | [`ada-eve`](ada-eve/) | 2,811 checks in C and Ada, 153 golden checks, GNAT agrees on 10 layouts, 13 compile-fail cases + control, fail-closed exit 70, fuzzed under ASan + UBSan |
| apl-kron | APL (glyphs, ⎕IO 0) and K (ngn/k-style ASCII) array expressions | KRON (exact rank-2 rationals and four-valued truth) | [`apl-kron`](apl-kron/) | 135,621 checks, 5,866 golden cases from Python fractions, APL/K digest equivalence, bindings tested against ngn/k and GNU APL, fuzzed under ASan + UBSan |
| mumps-myra | M (MUMPS) globals: typed subscripts and ^-piece values (GT.M / YottaDB) | MYRA (one line per node) | [`mumps-myra`](mumps-myra/) | 628,012 checks, golden files written by GT.M, GT.M interop and a 0-difference cross-check, fuzzed under ASan + UBSan |

Status codes: the pillars own -2001 and below (cobol-sutra uses -2001 to
-2099, fortran-trebago -2101 to -2199, ada-eve -2201 to -2299, apl-kron
-2301 to -2399, mumps-myra -2401 to -2499). Codes 0 to -99 are libzxv's and
pass through unchanged.

CI: [`.github/workflows/sdk-pillars.yml`](../../.github/workflows/sdk-pillars.yml).

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.
