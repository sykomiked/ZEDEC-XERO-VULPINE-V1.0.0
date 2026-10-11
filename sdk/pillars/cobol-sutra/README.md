<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# cobol-sutra: COBOL copybooks and Sutra over libzxv

The first legacy-language pillar. If you have COBOL records, this lets ZXV
read and write them **exactly**, and post them to a libzxv ledger.

- **The copybook is the treaty.** libzsutra reads a COBOL copybook (a
  documented subset of the data division) and gets a record layout: where
  every field is, how many bytes it takes, and how its bytes encode a
  value.
- **Sutra is the short form.** The same layout in one readable line per
  field. A copybook and a Sutra file that describe the same record compile
  to the identical layout, with the same SHA3-256 digest.
- **Values are exact.** Zoned decimal, COMP binary and COMP-3 packed
  decimal become an `int64` plus an implied scale, or a libzxv rational.
  There are no floats anywhere. Anything that does not fit exactly is
  refused, never rounded or truncated.
- **Postings go through a capability.** A payment record becomes one
  ledger posting through `zxv_ledger_post_cap`, with a capability you pass
  in. Keeping the books balanced (A = L + E) is the ledger's job. When it
  refuses a posting, you get its status back.

```text
      * PAYMENT.CPY                                # payment.sutra
       01  PAYMENT-REC.                            record payment-rec
           05  PAY-REF        PIC X(16).             pay-ref: x(16)
           05  PAY-AMOUNT     PIC S9(13)V99 COMP-3.  pay-amount: s9(13)v99 comp-3
           05  PAY-CCY        PIC X(3).              pay-ccy: x(3)
           05  PAY-FROM       PIC 9(9) COMP.         pay-from: 9(9) comp
           05  PAY-TO         PIC 9(9) COMP.         pay-to: 9(9) comp
           05  PAY-VALUE-DATE PIC 9(8).              pay-value-date: 9(8)
           05  PAY-FEE        PIC S9(5)V99.          pay-fee: s9(5)v99
           05  PAY-STATUS     PIC X.                 pay-status: x
               88  PAY-NEW    VALUE 'N'.
           05  FILLER         PIC X(4).              filler: x(4)
```

```text
$ zsutra same examples/payment.cpy examples/payment.sutra
same record: examples/payment.cpy and examples/payment.sutra, sha3-256 a92f904e28b5...
```

Pillar API 0.1 (`include/zsutra.h`). It may still change before 1.0. It
calls only the libzxv ABI 1.3 (`sdk/libzxv/include/zxv_api.h`), which the
rest of ZXV uses too: [ABI.md](../../docs/ABI.md),
[SDK_DESIGN.md](../../docs/SDK_DESIGN.md).

## Quickstart

```sh
make -C sdk/libzxv all                       # the C library every SDK wraps
make -C sdk/pillars/cobol-sutra test demo    # tests, then the demo
sdk/pillars/cobol-sutra/build/zsutra layout sdk/pillars/cobol-sutra/examples/statement.cpy
```

With GnuCOBOL installed (`apt-get install gnucobol`):

```sh
make -C sdk/pillars/cobol-sutra cobol golden-cobol
```

The demo:

```text
examples/payment.cpy and examples/payment.sutra declare the same record: 55 bytes, sha3-256 a92f904e...
PAY-AMOUNT -1234.56 is COMP-3 bytes 000000000123456d = -123456 / 10^2 = -30864/25
1/3 into PAY-AMOUNT: ZS_E_FIT (never rounded)
post FUND-ALICE    1000.00 NGN -> entry 1 (100000 minor units)
post PAY-0001       250.25 NGN -> entry 2 (25025 minor units)
post PAY-0002      5000.00 NGN -> ZXV_E_FUNDS
post PAY-0001         1.00 NGN -> ZXV_E_DUPLICATE
post PAY-0003         0.00 NGN -> ZXV_E_RANGE
post PAY-0004        12.34 USD -> ZXV_E_CURRENCY
equity in kobo: bank -100000, alice 74975, bob 25025 (sum 0)
ledger check: ZXV_OK
```

## How a copybook maps to exact values

A numeric field is an integer of at most 18 digits with an implied
decimal point. `PIC S9(5)V99` holding -123.45 is the integer -12345 with
scale 2. libzsutra hands you exactly that (`zs_get_decimal`), or the
reduced rational -2469/20 (`zs_get_rat`), or the text `"-123.45"`
(`zs_get_value`, which always prints all of the field's decimals).

| Picture and usage | Bytes | -123.45 in `S9(5)V99` looks like |
|---|---|---|
| `DISPLAY` (default) | one ASCII digit per byte; the sign is overpunched on the last digit | `30 30 31 32 33 34 4E` ("001234N", IBM convention) |
| `SIGN LEADING` | overpunched on the first digit | |
| `SIGN ... SEPARATE` | a `+` or `-` byte before or after the digits (+1 byte) | `2D 30 30 31 32 33 34 35` with `SIGN LEADING SEPARATE` |
| `COMP`, `BINARY`, `COMP-4` | big-endian binary: 2 bytes for 1-4 digits, 4 for 5-9, 8 for 10-18; two's complement when signed | `FF FF CF C7` |
| `COMP-3`, `PACKED-DECIMAL` | two digits per byte, the sign in the last nibble | `00 12 34 5D` |
| `PIC X(n)` | n bytes of text | |

COMP-3 in detail: `S9(5)V99` has 7 digits, so 7 digit nibbles plus a sign
nibble make 4 bytes: -123.45 is `00 12 34 5D`. Positive is `C`, negative
`D`, unsigned `F`; on input, IBM's `A` and `E` (positive) and `B`
(negative) are accepted too. When a field has an even number of digits,
the unused first nibble must be 0.

**Overpunched signs come in two conventions**, and the bytes alone cannot
tell them apart. Pick one with `zs_layout_set_overpunch`. It belongs to
the data, so it is not part of the layout's digest.

| Convention | +0..+9 | -0..-9 | Written by |
|---|---|---|---|
| `ZS_OVERPUNCH_IBM` (default) | `{ A B C D E F G H I` (a plain digit also reads as +) | `} J K L M N O P Q R` | mainframe data transferred as text; GnuCOBOL `-fsign=EBCDIC` |
| `ZS_OVERPUNCH_ASCII` | `0`..`9` | `p`..`y` | GnuCOBOL's default, Micro Focus |

The wrong convention is refused (`ZS_E_DATA`), never guessed. The golden
tests check both against records written by GnuCOBOL.

**What is refused, and how you find out:**

| You do | You get |
|---|---|
| read a field whose bytes are not a valid value (a space in a numeric field, a bad nibble, a binary value larger than the picture allows) | `ZS_E_DATA`, with the field and the byte in `zs_last_error` |
| write 1.234 into `V99`, 100000 into `9(5)`, -1 into an unsigned field, or 9 characters into `X(8)` | `ZS_E_FIT`, and nothing is written |
| write 1.230 into `V99` | accepted: the extra digit is zero, so nothing is lost |
| write 1/3 (`zs_put_rat`) | `ZS_E_FIT`. 1/4 is accepted (0.25). |
| write the double nearest 0.1, from `zxv_rat_from_ieee754` | `ZS_E_FIT`: it is 3602879701896397/36028797018963968, not 0.1 |

Binary fields follow `TRUNC(STD)`: a `9(4) COMP` holds 0..9999, even
though two bytes could hold 65535. A larger value in the record is
`ZS_E_DATA`.

## Sutra

```text
sutra   = header { field }
header  = "record" name                              (in column 1)
field   = indent name [ "[" count "]" ] ":" [ spec ]
spec    = picture { usage | sign }
usage   = "display" | "comp" | "binary" | "comp-3" | "packed"
sign    = "sign" ( "leading" | "trailing" ) [ "separate" ]
comment = "#" to the end of the line
```

- One field per line. A field with no picture is a group, and the fields
  indented under it are its items. Siblings line up exactly, like Python.
  Indent with spaces; tabs are refused.
- `name[3]:` is `OCCURS 3`. `filler:` is `FILLER`.
- Pictures are COBOL's: `x(12)`, `9(8)`, `s9(13)v99`, `sv9(6)`, `99`.
- Names and keywords are not case-sensitive. A name follows COBOL's rules
  (1-30 letters, digits and hyphens) and cannot be a word the copybook
  parser reserves (`to`, `comp`, `value`, ...), so every Sutra file can be
  printed as a copybook.

The canonical form of any layout is Sutra text (`zs_layout_sutra`, or
`zsutra layout FILE`): lower-case names, pictures written out
(`s9(13)v9(2)`), defaults left out, no comments. The digest is the
SHA3-256 of that text. `zs_layout_copybook` prints the same layout as a
fixed-format copybook (levels 01, 05, 10, ...). Both print forms parse
back to the same digest; the tests and the fuzzer check this.

## Supported COBOL

| Supported | Notes |
|---|---|
| levels 01-49 | one 01 record per copybook; a sibling's level must match the level of the items before it |
| `88` condition names | accepted and skipped (they take no storage) |
| `PIC X(n)`, `PIC [S]9(i)[V9(d)]` | at most 18 digits; repeats `9(5)` or spelled out `99999`; any letter case |
| `USAGE DISPLAY`, `COMP`, `COMPUTATIONAL`, `BINARY`, `COMP-4`, `COMP-3`, `COMPUTATIONAL-3`, `PACKED-DECIMAL` | with or without the word `USAGE`; a group's usage applies to its items |
| `SIGN [IS] LEADING / TRAILING [SEPARATE [CHARACTER]]` | DISPLAY items; with or without the word `SIGN` |
| `OCCURS n [TIMES] [INDEXED BY names]` | fixed tables, up to 7 nested; index names are skipped |
| `VALUE` / `VALUES` | skipped: an initial value does not change the layout |
| `FILLER`, or no name at all | never found by name |
| fixed format | columns 1-6 and 73-80 ignored; `*`, `/` and `D` in column 7 are comment lines |
| free format | `ZS_CB_FREE`; `*>` comments in both formats |

| Refused (`ZS_E_UNSUPPORTED`) | Why |
|---|---|
| `REDEFINES` | one byte, one meaning. Two views of the same bytes need a rule for which one is true, and that rule lives in the program, not the copybook. Declare each view as its own layout. |
| `OCCURS ... DEPENDING ON`, `OCCURS n TO m`, `KEY` | variable-length records |
| `COMP-1`, `COMP-2`, `FLOAT-*` | floating point; ZXV has no floats (R11) |
| `COMP-5`, `COMP-X` | native byte order; use `COMP` (big-endian) |
| `66 RENAMES`, `77` items, a second `01`, an elementary `01`, a copybook that starts at level 05 | one record per layout, with items under it |
| `SYNCHRONIZED`, `JUSTIFIED`, `BLANK WHEN ZERO`, `EXTERNAL`, `GLOBAL` | change the layout or the display, not the data |
| `INDEX`, `POINTER`, `NATIONAL`, and `A`, `N`, `G`, `P` and edited pictures (`Z`, `.`, `,`, `$`, `CR`, ...) | not record data, or not in this subset |
| continuation lines (`-` in column 7), tabs in a fixed-format copybook | expand tabs; join continued literals |
| the same data name twice | qualification (`A OF B`) is not supported |

Every refusal names the line and column and says what was expected:

```text
examples/broken.cpy:8:28: REDEFINES is not supported: every byte of a record has one meaning here [ZS_E_UNSUPPORTED]
             05  BR-AMOUNT-X REDEFINES BR-AMOUNT PIC X(5).
                             ^
```

Data names are checked against COBOL's rules and the words this parser
reserves, not against GnuCOBOL's or Enterprise COBOL's full reserved-word
lists.

## Using it from C

```c
#include "zsutra.h"

zs_layout *l;  zs_diag d;
if (zs_copybook_parse(text, len, ZS_CB_FIXED, &l, &d) != ZXV_OK)
    fprintf(stderr, "%s\n", d.msg);          /* "line 8, column 28: REDEFINES ..." */

int64_t units;  uint32_t scale;
zs_get_decimal(l, rec, rec_len, "PAY-AMOUNT", &units, &scale);   /* -123456, 2 */
zs_get_value(l, rec, rec_len, "ST-LINE-AMOUNT(2)", buf, sizeof buf, &n);  /* tables: 1-based */
zs_put_value(l, rec, rec_len, "PAY-FEE", "-1.50", 5);           /* exact text in */
zs_put_rat(l, rec, rec_len, "PAY-AMOUNT", 1, 3);                 /* ZS_E_FIT */
zs_record_check(l, rec, rec_len);            /* every numeric item, every occurrence */
zs_layout_destroy(l);
```

Status codes are libzxv's (`ZXV_E_*`, passed through) plus four of the
pillar's own: `ZS_E_SYNTAX` (-2001), `ZS_E_UNSUPPORTED` (-2002),
`ZS_E_DATA` (-2003) and `ZS_E_FIT` (-2004). Text outputs use libzxv's
two-call buffers (R4). Out parameters are written only on success, except
`zs_post`'s `out`, which is filled as far as the record could be read so
you can say which reference was refused.

## The posting bridge

```c
zs_posting_map map = {
    .amount = "PAY-AMOUNT", .from = "PAY-FROM", .to = "PAY-TO",
    .currency = "PAY-CCY", .reference = "PAY-REF",
    .minor_units = -1,                 /* take them from ISO 4217 */
};
zxv_cap *poster;                       /* LEDGER_POST | LEDGER_READ, nothing else */
zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &poster);
zs_posting p;
int32_t st = zs_post(poster, l, &map, rec, rec_len, &p);
```

`zs_post` reads the amount, converts it exactly to the currency's minor
units, reads the two account numbers and the reference, and calls
`zxv_ledger_post_cap`. `zs_posting_read` does the same without posting.

| Status | Meaning |
|---|---|
| `ZXV_OK` | posted; `p.entry_id` is the ledger's entry |
| `ZS_E_FIT` | 12.50 JPY (yen has no decimals), 1.005 NGN, account 1.5 or 2^32. Never rounded. |
| `ZS_E_DATA` | bad bytes in one of the mapped fields |
| `ZXV_E_CAP` | the capability is missing `LEDGER_POST`, revoked or unattested |
| `ZXV_E_FUNDS` | a holder account would go below zero |
| `ZXV_E_DUPLICATE` | the reference was posted before |
| `ZXV_E_RANGE` | the amount is zero, negative or above 2^53-1 |
| `ZXV_E_CURRENCY` | no implicit FX, not enabled, or not in ISO 4217 (set `minor_units` for a private unit) |
| `ZXV_E_NOT_FOUND` | an unknown account |
| `ZXV_E_POISONED` | the context halted; FATAL, never retry |

Inside one process a capability is least-privilege discipline, not a
sandbox ([ABI.md R10](../../docs/ABI.md#r10-capabilities-new-in-11)).

## GnuCOBOL interop

[`cobol/PAYPOST.cbl`](cobol/PAYPOST.cbl) copies `payment.cpy`, fills its
own records with `MOVE`, and calls libzsutra through a thin C shim,
[`cobol/zscob_shim.c`](cobol/zscob_shim.c). The record goes `BY REFERENCE`
exactly as the copybook lays it out. libzsutra reads it with the same
copybook, and the shim posts it with an attenuated capability. COBOL only
ever sees status names.

```text
$ make cobol
FUND-ALICE 1000.00 NGN (fee 0.00) -> entry 1
PAY-0001 250.25 NGN (fee -1.50) -> entry 2
PAY-0002 5000.00 NGN (fee 0.00) -> ZXV_E_FUNDS
PAY-0001 1.00 NGN (fee 0.00) -> ZXV_E_DUPLICATE
PAY-0003 -7.25 NGN (fee -12.34) -> ZXV_E_RANGE
PAY-0004 10.00 USD (fee 0.00) -> ZXV_E_CURRENCY
equity bank: -100000 kobo
equity alice: 74975 kobo
equity bob: 25025 kobo
ledger check: ZXV_OK
GnuCOBOL interop: output matches cobol/paypost.expected
```

| COBOL | C |
|---|---|
| the 01 record | `unsigned char *` |
| `PIC X(n)` | `char *`, space-padded, not NUL-terminated |
| `PIC S9(9) COMP-5` | `int32_t *` |
| `PIC S9(18) COMP-5` | `int64_t *` |

Compile COBOL with **`-fbinary-size=2-4-8 -fbinary-byteorder=big-endian`**
so `COMP` matches a mainframe (and this pillar). GnuCOBOL's default packs a
`9(2) COMP` into one byte. The overpunch convention must match too: the
shim is told which one (`SIGN-STYLE` in PAYPOST).

`make golden-cobol` has GnuCOBOL alone ([`cobol/GOLDEN.cbl`](cobol/GOLDEN.cbl),
no libzsutra) write six records, once with the ASCII sign and once with
`-fsign=EBCDIC`. The output must equal `test/golden/records-*.hex`.
`test_golden` reads those records with both the copybooks and their Sutra
twins, compares every value with `records.expect`, and rebuilds every
record from those values. The rebuilt bytes must equal GnuCOBOL's, byte
for byte.

## Tests

`make test` runs:

| Test | Checks | What |
|---|---|---|
| `test_copybook` | 290 | sizes and offsets, fixed and free format, sequence areas, tables and references, 52 refusals each at its line and column, the copybook printer round trip |
| `test_sutra` | 168 | Sutra and copybook give identical layouts (digest, canonical text, every field), spellings, canonical form, 33 refusals |
| `test_codec` | 153,547 | 26 hand-computed byte vectors, 15 kinds of bad bytes, fits and refusals, rationals, whole records, and a seeded round trip over 486 layouts (every usage, sign, 1-18 digits, three scales) x 80 values |
| `test_golden` | 520 | GnuCOBOL records in both sign conventions, read and rebuilt byte for byte |
| `test_post` | 44 | postings and every refusal status, minor units, private units, a poisoned context |
| `fuzz_parse` | 3,000 inputs | the smoke run (see below) |

Plus the R11 check (`check-nofloat`: no float type in any source; the
sources are also compiled with `-mgeneral-regs-only` where the compiler
supports it), `zsutra same` on both example pairs, and the two broken
examples, which must be refused.

The fuzzer feeds every input to the Sutra parser and to the copybook
parser in both formats. It checks determinism, error positions, that the
canonical Sutra and the printed copybook parse back to the same digest,
that every field lies inside its parent, and the codec on every numeric
field of every accepted layout. `make fuzz FUZZ_ITERS=250000` runs it
longer; CI runs 4 x 250,000 inputs under ASan and UBSan, and libFuzzer for
60 seconds where clang ships it.

```sh
make test                     # everything above
make SANITIZE=1 OUT=build/san test
make fuzz FUZZ_ITERS=250000   # ZS_FUZZ_SEED=n for another stream
make cobol golden-cobol       # needs cobc
```

CI: [`.github/workflows/sdk-pillars.yml`](../../../.github/workflows/sdk-pillars.yml)
(ubuntu-24.04 gcc and clang, ASan + UBSan with both, macos-14 clang,
GnuCOBOL on ubuntu-24.04).

## Honest limits

- **ASCII records only.** No EBCDIC code pages. Text from a mainframe
  must be converted to ASCII first, and overpunched signs then arrive in
  the IBM convention. COMP and COMP-3 bytes are the same in both, but they
  must not go through a text conversion.
- **No REDEFINES, no OCCURS DEPENDING ON, no variable-length records.**
- **Data only.** No CICS, IMS, DB2, JCL, VSAM or file handling. Your
  program reads the records; libzsutra interprets them.
- **The shim is an example**, wired to the field names of `payment.cpy`.
  It is not a general COBOL binding.
- **This is not a mainframe certification.** It is tested against
  GnuCOBOL 3.1.2 and hand-computed bytes, not against IBM Enterprise
  COBOL.
- **Capabilities are discipline in-process.** Enforcement needs a process
  boundary (`zxvd`).

## Files

| Path | What |
|---|---|
| `include/zsutra.h` | the pillar API |
| `src/zs_copybook.c`, `src/zs_sutra.c`, `src/zs_picture.c` | the two front ends and the shared picture parser |
| `src/zs_layout.c` | validation, sizes, offsets, canonical text, digest, printers, references |
| `src/zs_codec.c` | bytes <-> exact values |
| `src/zs_post.c` | the posting bridge |
| `tools/zsutra_tool.c` | `zsutra layout / copybook / fields / same / decode` |
| `examples/` | `payment` and `statement` as copybook and Sutra, two broken files, the demo |
| `cobol/` | `PAYPOST.cbl` and its shim, `GOLDEN.cbl`, the expected output |
| `test/` | the tests, the fuzzer, `golden/` records from GnuCOBOL |

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.
