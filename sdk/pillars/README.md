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

Status codes: the pillars own -2001 and below (cobol-sutra uses -2001 to
-2099). Codes 0 to -99 are libzxv's and pass through unchanged.

CI: [`.github/workflows/sdk-pillars.yml`](../../.github/workflows/sdk-pillars.yml).

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.
