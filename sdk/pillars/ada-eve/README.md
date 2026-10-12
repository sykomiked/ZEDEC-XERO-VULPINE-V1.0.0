<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ada-eve: an exact Ada binding for libzxv, and Eve

This is the third legacy-language pillar. It is for people who keep
their record layouts in Ada package specifications and want two things:
to exchange those records exactly, and to call libzxv from Ada without
floats or unchecked authority. It has two parts:

- **package `Zxv`** (`ada/`). An Ada 2012 binding over libzxv, written
  with `Interfaces.C` and imported C functions. It provides:
  - an exact `Rational` type with operators;
  - a four-valued `Truth` that never turns into a `Boolean` by itself;
  - the 64 `Hexagram` tags;
  - capabilities as limited controlled types;
  - the ledger and the quarantine log, used only through `*_cap` calls;
  - integer-with-scale decimals: `Zxv.Fixed`, written in SPARK, and the
    generic `Zxv.Decimals`.

  There is no `Float` anywhere except one marked, explicit conversion.
  A FATAL status stops the program with exit status 70, and no handler
  can catch it.
- **Eve** (`src/`, `Zxv.Eve`). A short way to write the same type
  declarations. libzeve, a C library over libzxv, parses Eve and an Ada
  subset into one canonical layout, so two files are equivalent exactly
  when their SHA3-256 digests match. libzeve also reads and writes values
  of those types exactly, by path, and posts a record to the ledger
  through a capability.

```ada
--  examples/payments.ads (Ada)                 # examples/payments.eve (Eve)
package Payments is                             eve payments
   type Currency is (NGN, GHS, KES, ZAR, USD);  currency = (ngn, ghs, kes, zar, usd)
   type Account_Id is range 0 .. 2**32 - 1;     account_id = 0 .. 2**32 - 1
   type Money is delta 0.01 digits 15;          money = dec(15, 2)
   subtype Fee is Money                         fee = money -999.99 .. 999.99
     range -999.99 .. 999.99;
   type Line_No is range 1 .. 4;                line_no = 1 .. 4
   type Lines is array (Line_No) of Money;      lines = [line_no] money
   type Payment is record                       payment {
      Ref      : String (1 .. 16);                ref: str(16)
      Amount   : Money;                           amount: money
      Ccy      : Currency;                        ccy: currency
      From, To : Account_Id;                      from, to: account_id
      ...                                         ...
```

```console
$ build/zeve same examples/payments.ads examples/payments.eve
same types: examples/payments.ads and examples/payments.eve, sha3-256 2c3a553116257c1d64a240a9d44ce63bbc61ccabd2a03b0595006af5ba945b46
```

```ada
L : Zxv.Eve.Layout;
L.Load ("examples/payments.eve");
declare
   T : constant Zxv.Eve.Type_Ref := L.Find ("payment");
   V : Zxv.Eve.Bytes := L.New_Value (T);       --  74 bytes, canonical encoding
begin
   L.Put_Text (T, V, "amount", "1234.56");     --  stored as the integer 123456
   L.Put_Rational (T, V, "breakdown(1)", Zxv.Rationals.To_Rational (1, 4));  --  0.25
   L.Put_Rational (T, V, "amount", Zxv.Rationals.To_Rational (1, 3));
   --  raises Refused_Error: ZE_E_FIT [-2206] ... 1/3 has no exact value with
   --  2 decimals (never rounded)
end;
```

## The pillar pattern

1. **The legacy format is the treaty.** The layout is an ordinary Ada
   package specification, and you do not rewrite it. GNAT compiles every
   accepted file (and every printed one), and `make golden-gnat` checks
   that GNAT agrees with libzeve on every bound, digit count, scale,
   literal and array length.
2. **A shorthand compiles to the same thing.** An Eve file and an Ada
   spec that declare the same types have the same canonical text. That
   text is printed in Eve, and its SHA3-256 digest is the layout's
   identity. Spelling, case, comments, `2**32 - 1` against `4294967295`,
   and `From, To : T` against two separate lines do not change the
   digest. Any change to a range, scale, literal, order or name does.
3. **A parser in C over libzxv.** libzeve (`include/zeve.h`) is plain
   C11. It calls only the libzxv ABI (`zxv_sha3_256`, `zxv_rat_*`,
   `zxv_iso4217_by_alpha`, `zxv_ledger_post_cap`, status names) and
   runs in your process.
4. **Exact results only.** Numbers are integers with an implied scale.
   A `delta 0.01 digits 15` value is the integer 123456 with scale 2,
   never a binary fraction. A value with more decimals than the type is
   refused (`ZE_E_FIT`), not rounded. A text longer than its `String`
   is refused, not truncated.
5. **State changes go through capabilities.** `ze_post` (and
   `Zxv.Eve.Post`) reads a record into minor units and posts it with
   `zxv_ledger_post_cap`. The ledger checks the capability on every
   call, and its refusals come back unchanged.

## Honest limits

- **A subset of Ada, not Ada.** The front end accepts:
  - `package P [with Pure] is ... end P;`, with `pragma Pure` or
    `pragma Preelaborate`;
  - integer types `range L .. H` (static expressions with
    `+ - * / mod rem **`, based and grouped literals);
  - decimal fixed-point types `delta 10.0**(-s) digits d [range L .. H]`,
    with d at most 18;
  - enumerations;
  - `String (1 .. n)`;
  - constrained arrays of up to 4 dimensions;
  - records;
  - subtypes with a range.

  Everything else is refused with `ZE_E_UNSUPPORTED`, a line, a column
  and a reason, and never read as something close. That includes
  modular, floating-point and ordinary fixed-point types, access types,
  derived types, tagged types, discriminants, variants and `null record`.
  It also includes aspects, representation clauses, private parts, child
  packages, `with` clauses, objects and named numbers. Defaults (`:= 0`)
  are accepted and ignored.

  An index is a named discrete type, a literal range, or
  `Integer | Natural | Positive range L .. H`. For any other ranged
  index, declare a subtype.
- **The encoding is the pillar's, not GNAT's.** Scalars are big-endian,
  each in the narrowest of 1, 2, 4 or 8 bytes that holds its range
  (signed only when the range has negatives). Enumerations store their
  position. Records are packed in declaration order and arrays are
  row-major. This is a portable exchange format. Do not overlay it on
  an Ada object with `Unchecked_Conversion` or an address clause: GNAT's
  in-memory layout is different and differs between targets.
- **Package names are not checked against the GNAT library.** A layout
  named like a predefined unit (`Calendar`, `Text_IO`) parses, but GNAT
  then cannot build a program that uses it. The golden test that first
  showed this is now called `shifts`.
- **SPARK covers only `Zxv.Fixed`.** `make prove` ran gnatprove 14.1.1
  (from Alire 2.0.2, with Why3 1.6.0, Alt-Ergo 2.4.0, cvc5 1.1.2 and Z3)
  at `--level=2`. It proved all 86 checks of `Zxv.Fixed` with none
  unproved:
  - 56 run-time checks;
  - 12 assertions;
  - 6 functional contracts;
  - 4 initialization checks;
  - 8 termination checks.

  That means freedom from run-time errors, plus the postconditions as
  written (for example, `Rescale` multiplies or divides by exactly
  `10**k`). It does **not** prove that `Image` and `Value` are inverses,
  and nothing else in the binding is SPARK or proved. The other
  packages have `Pre` and `Post` contracts, which are checked at run
  time with `-gnata` (as the tests build). Turning assertions off turns
  those checks off, but not the status checks or the FATAL stop.
- **No certification.** This is not DO-178C, IEC 61508, ISO 26262 or EN
  50128 material, and makes no such claim. The tests and the one proof
  above are what back it.
- **The FATAL stop is a C `exit(70)`.** Pending Ada finalization does not
  run, so handles are not released. The process ends, which is the
  point: nothing may run after a poisoned context.
- **In-process authority is discipline, not a sandbox** (ABI.md, R10).
  `Zxv.Authority.Interop.Capability_Handle` exposes the C handle on
  purpose, for C calls like `ze_post`. Anything in the process can reach
  memory. The capability rules are enforced across a process boundary,
  by zxvd.
- **Ledger limits are libzxv's.** An account is a whole number in
  0 .. 4294967295. A reference has at most 35 bytes. A currency must be
  ISO 4217 (or registered, with `minor_units` given). A lower-case
  String `ngn` is not `NGN`, while an enumeration literal is upper-cased.
- **Tested on Linux only for Ada.** CI runs the Ada half on ubuntu-24.04
  with apt's GNAT. On macos-14 it runs the C half only, because Homebrew
  core has no GNAT. Nothing was tested on Windows. The `ada-eve-prove`
  CI job is written but has not run yet, and the proof above was run
  locally. clang here has no libFuzzer runtime, so `make fuzz-libfuzzer`
  has not run locally.
- **Concurrency.** A parsed `Layout` is not changed by any value call,
  and the last-error detail is per thread. Concurrent use has not been
  tested.

## Quickstart

```sh
make -C ../../libzxv all
make test                  # C + Ada; without GNAT: C only (ADA=0)
make demo                  # examples/eve_demo.adb
make SANITIZE=1 OUT=build/san test
make prove                 # needs gnatprove on PATH
```

On Ubuntu, `sudo apt-get install gnat` is enough for everything except
`make prove`. gnatprove is not packaged in apt. Get it with
[Alire](https://alire.ada.dev/) (`alr get gnatprove`), then put its
`bin/` on `PATH`.

The `zeve` tool:

```text
zeve check FILE...          parse; digest and type count
zeve canon FILE             canonical Eve text
zeve ada FILE               the same layout as an Ada spec
zeve same A B               equal digests? (exit 0) or not (exit 1)
zeve types FILE             kinds, ranges, encoded sizes, component offsets
zeve decode FILE TYPE HEX   every scalar of one encoded value
zeve golden FILE...         run golden scripts
zeve probe FILE             an Ada main printing what GNAT knows (golden-gnat)
```

## Eve

One declaration per line, with `#` comments. Names follow Ada's rules,
and Ada's reserved words and predefined names (`integer`, `string`,
`float`, …) cannot be declared. So every Eve file is also a valid Ada
spec (`zeve ada`).

| Eve | Ada |
|---|---|
| `eve payments` | `package Payments is ... end Payments;` |
| `x = 0 .. 2**32 - 1` | `type X is range 0 .. 2**32 - 1;` |
| `money = dec(15, 2)` | `type Money is delta 0.01 digits 15;` |
| `rate = dec(5, 4) -1 .. 1` | `type Rate is delta 0.0001 digits 5 range -1.0 .. 1.0;` |
| `ccy = (ngn, usd)` | `type Ccy is (NGN, USD);` |
| `name = str(16)` | `subtype Name is String (1 .. 16);` |
| `fee = money -999.99 .. 999.99` | `subtype Fee is Money range -999.99 .. 999.99;` |
| `lines = [line_no] money` | `type Lines is array (Line_No) of Money;` |
| `row = [12] money` | `type Row is array (1 .. 12) of Money;` |
| `grid = [ccy, 1 .. 3] money` | `type Grid is array (Ccy, 1 .. 3) of Money;` |
| `p { a, b: x; s: ccy ngn .. ngn }` | `type P is record A, B : X; S : Ccy range NGN .. NGN; end record;` |

A decimal type is declared on its own line and named, as in Ada. A
record component is a named type, optionally with a range, or `str(n)`.

**Canonical text** (what the digest covers):
- the names are lower case;
- one component per line, indented two spaces;
- integer bounds are written in decimal;
- decimal bounds are written with all the type's decimals;
- a range equal to the base type's range is left out;
- `[n]` is written `1 .. n`.

## The C API (`include/zeve.h`)

| Call | |
|---|---|
| `ze_ada_parse`, `ze_eve_parse` | text → `ze_pkg`, or a status with line, column and message (`ze_diag`) |
| `ze_pkg_eve`, `ze_pkg_ada`, `ze_pkg_digest`, `ze_pkg_same` | canonical text, printed Ada, SHA3-256, equivalence |
| `ze_type_*`, `ze_array_dim`, `ze_component_at`, `ze_enum_literal` | the types: kind, range, digits, scale, size, offsets |
| `ze_find`, `ze_get_*`, `ze_put_*` | values by path (`"grid(usd, 2).qty"`), exactly; nothing is written on a refusal |
| `ze_value_init`, `ze_value_check` | the canonical initial value; check every scalar (`ZE_E_DATA` names the path) |
| `ze_posting_read`, `ze_post` | one record → one ledger posting through `zxv_ledger_post_cap` |

Buffers follow the R4 two-call rule. Status codes:

| Code | Name | Class | Meaning |
|---|---|---|---|
| -2201 | `ZE_E_SYNTAX` | USAGE | malformed text |
| -2202 | `ZE_E_UNSUPPORTED` | USAGE | valid Ada outside the subset |
| -2203 | `ZE_E_NAME` | USAGE | unknown, duplicate, reserved or predefined name |
| -2204 | `ZE_E_CONSTRAINT` | USAGE | an empty or out-of-base range, too many digits, a bound that is not exact |
| -2205 | `ZE_E_DATA` | REFUSED | an encoded value holds a scalar outside its type |
| -2206 | `ZE_E_FIT` | REFUSED | a value does not fit its type exactly |
| -2207 | `ZE_E_LIMIT` | REFUSED | text, types, dimensions or a value larger than the limits |

Codes 0 to -99 are libzxv's and pass through unchanged.

## Package `Zxv`

| Package | |
|---|---|
| `Zxv` | `Status_Code`, the codes above, `Status_Class`, `Usage_Error` / `Refused_Error` / `System_Error`, `Status_Of` |
| `Zxv.Rationals` | `Rational` (reduced, `Type_Invariant`), `+ - * / ** abs`, comparisons, `Parse`, `Image`, `To_Q16`, `Sum`, `Dot`, `From_IEEE_754_Bits`, and the one `From_IEEE_754 (Long_Float)` |
| `Zxv.Logic` | `Truth` (`Neither`, `True`, `False`, `Both`), the kernel's `not` / `and` / `or`, `To_Boolean (T, On_Both, On_Neither)` |
| `Zxv.Hexagrams` | `Hexagram` (private), `Base`, `Polarity`, `Phase`, `Action`, `Step` / `Set` (`Pre`, plus status forms), `Forbid` |
| `Zxv.Authority` | `Context`, `Capability` (limited controlled), `Rights`, `Attenuate`, `Revoke`, `Set_Attestation`, `Require` |
| `Zxv.Authority.Ledger` | currencies, accounts, `Post` / `Try_Post`, `Balance_Of` (`Post`: equity = debit − credit), `Verify`, journal |
| `Zxv.Authority.Quarantine` | the hash-chained log of forbidden work |
| `Zxv.Fixed` (SPARK) | `Decimal` = value / 10**scale, 18 digits; `Rescale`, `Add`, `Compare`, `Image`, `Value` |
| `Zxv.Decimals` | `generic Scale, Digits_Count`: `type Amount is new Int64 range -Max .. Max` |
| `Zxv.Eve` | `Layout` (limited controlled): `Load`, `Parse_Ada`, `Parse_Eve`, `Same`, `Canonical`, `Bytes`, `Get_*` / `Put_*`, `Post` |

**Errors.** A failing call raises the exception for its status class.
The message starts with the status name and code:
`"ZXV_E_FUNDS [-6] Zxv.Authority.Ledger.Post: ..."`. `Status_Of`
recovers the code. The `Try_` and status-procedure forms return the
status instead.

**FATAL.** `ZXV_E_POISONED` and `ZXV_E_INVARIANT` are never exceptions,
in any form. The program prints the reason on standard error and exits
with status 70, even inside `when others` and even from `Try_Post`.
This is the counterpart of the Fortran pillar's `ERROR STOP 70`
(`make fatal`).

**No implicit conversions** (`make compile-fail`, 13 cases plus a
control). None of these compile:
- `R + 0.5` and `To_Rational (0.1)`;
- `R : Rational := 1` and `Integer_64 (R)`;
- `From_IEEE_754` with a `Float`;
- `if T then` and `Boolean (T)` on a `Truth`;
- a `Hexagram` assigned to a `Truth`, or an integer to a `Hexagram`;
- copying or assigning a `Capability` or a `Layout`;
- adding two different `Zxv.Decimals` instances.

**No floats** (`make check-nofloat-ada`). Outside one
`-- R11-BOUNDARY-BEGIN` / `-- R11-BOUNDARY-END` region per file, none of
these may appear in the binding, the tests or the demo:
- `Float` or `Long_Float`;
- a C float or double type;
- a real literal;
- an `is digits` or `is delta` declaration.

The binding has exactly one such region: `From_IEEE_754`, which passes
the double's 64 bits to `zxv_rat_from_ieee754` and does no
floating-point arithmetic.

## Tests

Counts from a clean `make test` (gcc 13.3, GNAT 13.3, Ubuntu 24.04):

| Suite | Checks | |
|---|---|---|
| `test_ada` (C) | 167 | the Ada subset: accepted forms, 35 refusals with status, line and column |
| `test_eve` (C) | 99 | equivalence with Ada, the canonical form, 10 one-change variants that must differ, 6 spellings that must not, 16 refusals |
| `test_codec` (C) | 190 | byte vectors for every width and sign, paths, every refusal leaves the bytes unchanged, 20,000 seeded round trips |
| `test_post` (C) | 32 | the posting bridge: minor units, currency, capability, funds, duplicate, poisoned context |
| golden (`zeve golden`) | 153 in 5 scripts | payments (byte for byte), shifts, decimal extremes, int64 limits, 10 refusals |
| `test_zxv` (Ada) | 2,115 | `sdk/conformance/vectors.json`: statuses, rationals, IEEE-754, Q16, the logic and 64×64 hexagram tables, polarity, the quarantine chain; capabilities, the ledger, lifetimes (R6), decimals |
| `test_eve_ada` (Ada) | 55 | `Zxv.Eve`: one layout from both forms, types, exact values, refusals, posting |
| `fatal` | 2 programs | exit 70 on `ZXV_E_POISONED`, from `Ledger.Try_Post` and from `Eve.Try_Post` in `when others` |
| `compile-fail` | 14 | 13 rejected with the expected diagnostic, 1 control compiles |
| `golden-gnat` | 10 layouts | GNAT compiles each and agrees on 120 facts |
| fuzz smoke | 3,025 inputs | both front ends, printers and codecs (`make fuzz` for more) |

Under `make SANITIZE=1 OUT=build/san test`, the same counts pass with
ASan and UBSan in the C library and tests, and ASan in the Ada programs.
Separately, `make SANITIZE=1 fuzz FUZZ_ITERS=250000` ran clean with
`ZE_FUZZ_SEED` 1 and 2 (250,025 inputs each, about 170,000 accepted per
run), as did six 60,000-input runs with other seeds.

The fuzzer's invariants:
- a second parse gives the same status and diagnostic;
- a refusal's message starts with its own `line L, column C:`;
- the canonical Eve and the printed Ada of every accepted input
  re-parse to the same digest;
- the canonical text is a fixed point;
- the initial value of every type passes `ze_value_check`;
- a refused put changes no byte;
- a value read back as text stores back to the same bytes.

## Layout

```text
include/zeve.h           the pillar API (libzeve)
src/                     lexer, Ada and Eve front ends, printer, codec, posting
ada/                     package Zxv and its children; prove.gpr (SPARK)
tools/zeve_tool.c        the zeve command
tools/gen_vectors_ada.py vectors.json -> an Ada package of constants
tools/check_nofloat_ada.py  R11 for Ada
tools/golden_gnat.sh     GNAT against libzeve
examples/                payments.ads / .eve, broken.ads / .eve, eve_demo.adb
test/                    C, Ada, golden, fuzz and compile-fail tests
ci-jobs.yml              the CI jobs, for .github/workflows/sdk-pillars.yml
```

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.
