# libzxv-legacy

Exact data transcoding between mainframe-era representations (COBOL,
Fortran, EBCDIC) and modern ones, as one static library and one header,
`zxv_legacy.h`.

| Header | What it is |
|---|---|
| `cobol.h` | COMP-3, zoned and binary codecs; copybook parser; fixed and RDW record readers |
| `fortran.h` | unformatted sequential record framing; IBM HFP <-> IEEE 754 on bit patterns |
| `ebcdic.h` | EBCDIC CCSID 037 / 500 / 1047 <-> UTF-8 |
| `lightningrod.h` | adapters into exact rationals, reporting lossless / lossy / no bridge |
| `rational.h` | the 64-bit exact rationals those convert into |

The point is that money stays exact: a COMP-3 amount becomes a scaled
integer or a rational, never a binary float, and a conversion that would
lose information says so (`lr_result_t.exact`) instead of rounding quietly.

## Build

```sh
make -C packages legacy          # build/libzxv-legacy/libzxv-legacy.a + include/
make -C packages check-legacy    # symbol check + smoke program
cc -Ipackages/build/libzxv-legacy/include app.c packages/build/libzxv-legacy/libzxv-legacy.a
```

Built in place from `kernel/src/legacy/{cobol,fortran,ebcdic}.c`,
`kernel/src/lightningrod/lightningrod.c` and `kernel/src/rational/rational.c`.
The telephony codecs that also live in `kernel/src/legacy` (SIP, SS7, TCAP,
ISUP, DTMF, GSM) are not part of this package.

## Properties

- No floating point types, no allocation.
- 64-bit targets only: `rational.c` computes with `__int128`, so the archive
  needs libgcc's (or compiler-rt's) 128-bit division helpers `__divti3`,
  `__modti3` and `__divmodti4`. `make check` allows those and fails on any
  other undefined symbol.
- Each header has an "HONEST LIMITS" section. In short: no COMP-1/COMP-2,
  no edited pictures, no SIGN SEPARATE, no nested OCCURS or OCCURS
  DEPENDING ON; single-byte EBCDIC only; HFP has no Inf/NaN; rationals
  are 64-bit over 64-bit and report overflow as invalid.

## Status: tested, not verified, not certified

Tested in `make -C kernel verify-all` by `test_legacy_cobol`,
`test_legacy_fortran`, `test_legacy_ebcdic`, `test_lightningrod`,
`test_lightningrod_regress`, `test_rational` and `test_rational_regress`,
plus this package's smoke program (copybook, COMP-3 round trip, EBCDIC,
HFP, and the lossy double path).

Not formally verified, not certified, and not checked against a real
mainframe's output by this project: compare results with your own system's
data before relying on them for a migration.
