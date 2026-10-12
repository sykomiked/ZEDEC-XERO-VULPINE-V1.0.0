<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Why ZXV carries forward COBOL, Fortran, Ada, APL/K and MUMPS

A new operating system usually asks the world to come to it. ZXV goes the
other way for five old languages. Each has a "pillar" under `sdk/pillars/`
that lets ZXV read that language's own data descriptions and values exactly,
without asking anyone to rewrite their programs. The pillar READMEs say what
each one does. This page says why.

## For the general reader

A large share of the software that runs banks, payroll, insurance, weather
and physics research, aircraft and rail systems, and hospital records was
written decades ago in a few languages:

- **COBOL** for business records and money;
- **Fortran** for numerical science;
- **Ada** for systems where a fault is expensive, such as transport;
- **APL and K** for compact array calculations, common in finance;
- **MUMPS (M)** for health-record databases.

That software works, and rewriting it is costly and risky. ZXV's design
philosophy ([`DESIGN_PHILOSOPHY.md`](../../DESIGN_PHILOSOPHY.md), section 4)
is "don't reinvent the wheel": build bridges to what works, not
replacements. Each pillar lets data written by those systems be read and
checked by ZXV with no rounding, and lets an amount of money become a ledger
entry that ZXV's books keep balanced.

Each pillar also has a short modern spelling of the same thing (Sutra for
COBOL, Trebago for Fortran, Eve for Ada, KRON for APL/K, MYRA for MUMPS).
If the old form and the short form describe the same record, they produce
the same fingerprint, so anyone can check that nothing changed in
translation.

## For the academic reader

The pillars are an exercise in language interoperability through a shared
canonical form, rather than through a common runtime or translation of
programs. The shared pattern ([`sdk/pillars/README.md`](../../sdk/pillars/README.md)):

1. **The legacy description is the treaty.** The pillar parses the
   language's own data description (COBOL copybook, Ada type declarations,
   M global map, and so on) as written.
2. **A shorthand DSL compiles to the same canonical layout.** Equivalence
   of a legacy file and a shorthand file is a SHA3-256 digest comparison of
   the canonical text. In KRON, APL and K spellings of one program share a
   digest, while APL's right fold and K's left fold do not: the canonical
   form keeps meaning, not glyphs.
3. **Parser in C over the libzxv ABI, run in userland.** Each pillar's
   library (for example libzsutra, libztrebago, libzeve, libzkron, libzmyra)
   calls only the libzxv ABI ([`sdk/docs/ABI.md`](../../sdk/docs/ABI.md))
   and runs in the caller's process. The architecture rules list this as
   R7: "Shorthand parser in libzxv, execution in userland, signed integer
   results back" ([`docs/ARCHITECTURE.md`](../ARCHITECTURE.md)).
4. **Exact values only.** Values are signed int64 with an implied decimal
   scale, or libzxv exact rationals (int64/int64). The ABI has no float or
   double parameters (ABI rule R11), and the kernel has no floating point
   (architecture rule R1). A host double enters only as raw IEEE-754 bits
   converted to the exact rational they denote. A value that does not fit
   is refused (`ZXV_E_RANGE` and pillar-specific codes), never rounded.
5. **State changes through capabilities.** A record that moves value
   becomes one ledger posting via a `*_cap` call with a capability the
   caller supplies; the ledger enforces balance and returns refusals as
   status codes.

From a DSL-design view, the shorthands are deliberately small: declarative
layout languages (Sutra, Eve, MYRA) and narrow expression languages
(Trebago's numeric kernels, KRON's rank-2 arrays with four-valued truth).
The interesting property is not expressiveness but checkable equivalence
with an existing, trusted notation, plus differential testing against
outside implementations (GnuCOBOL, GT.M, GNAT, ngn/k, GNU APL, Python's
`fractions`), as each pillar README documents.

## What actually exists

| Pillar | Language | Shorthand | Folder |
|---|---|---|---|
| cobol-sutra | COBOL copybooks (data division subset) | Sutra | `sdk/pillars/cobol-sutra/` |
| fortran-trebago | Fortran 2008+ via ISO_C_BINDING | Trebago | `sdk/pillars/fortran-trebago/` |
| ada-eve | Ada 2012 via Interfaces.C; `Zxv.Fixed` in SPARK | Eve | `sdk/pillars/ada-eve/` |
| apl-kron | APL glyphs and K-style ASCII | KRON | `sdk/pillars/apl-kron/` |
| mumps-myra | M (MUMPS) globals, GT.M / YottaDB | MYRA | `sdk/pillars/mumps-myra/` |

Test counts, golden sources and fuzzing are listed per pillar in
[`sdk/pillars/README.md`](../../sdk/pillars/README.md); CI is
`.github/workflows/sdk-pillars.yml`. An older, separate bridge for
mainframe and telephony data (EBCDIC, COMP-3, IBM hexadecimal floating point
as bit patterns, SS7/USSD framing) lives in `kernel/src/legacy/` and is
described in [`LEGACY_BRIDGE.md`](../LEGACY_BRIDGE.md).

## Limits

- **Data, not whole programs.** The pillars read and write data and run
  narrow expressions. They do not compile or run arbitrary COBOL, Fortran,
  Ada, APL or M programs. COBOL support excludes REDEFINES, OCCURS
  DEPENDING ON, variable-length records, CICS, IMS, DB2, JCL and VSAM.
- **Slower than floating point.** Trebago and KRON are interpreters over
  exact rationals; their READMEs report about two orders of magnitude
  behind native double arithmetic on the measured workloads. They are meant to sit next to float code for the values that
  must be exact, not to replace it.
- **No big integers.** Values are int64 or int64/int64; overflow is refused.
- **No certification.** cobol-sutra is tested against GnuCOBOL, not IBM
  Enterprise COBOL. ada-eve states it is not DO-178C, IEC 61508, ISO 26262 or
  EN 50128 material; SPARK proof covers only `Zxv.Fixed`.
- **Advisory only for safety-critical domains.** Architecture rule R8 and
  [`sdk/docs/SECURITY.md`](../../sdk/docs/SECURITY.md) state that
  life-safety, clinical and grid actions are advisory and human-approved.
  mumps-myra makes no HIPAA, clinical-safety, FDA or ONC claim; it checks
  the shape of data, not its meaning, and any clinical use is advisory with
  a qualified human's approval. Its examples use made-up data.
- **Capabilities in-process are discipline, not a sandbox.** Enforcement
  needs a process boundary (the `zxvd` daemon).
- **Pillar APIs are version 0.1** and may change before 1.0.
