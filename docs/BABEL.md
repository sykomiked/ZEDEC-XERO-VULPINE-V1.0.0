<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Babel: the bridges between languages and systems

Babel is the machine side of ZXV's compatibility engine. Its human side,
Rosetta, explains code in plain words and carries the spoken languages
(built in the spoken-languages work, PR #149; the command-line side is
docs/ROSETTA_SHELL.md). Both names are working
names until a trademark check clears them.

Babel does not speak any language natively. Each language keeps its own
front-end (Lingua, the five pillars, the SDKs, Z-Wasm). Babel fills the gaps
between them: it moves a value from one runtime to another, adapts how one
calls another, converts data formats and encodings, and catalogues the
protocol shims that already exist. Every bridge is checked by running both
sides; when they disagree the answer is BOTH and the call is held.

Status: steps 1 and the first half of 2 are built (kernel/src/babel, `make
test-babel`); section 7 gives the order of the rest.

## 1. One value between runtimes

Runtimes disagree about what a value is: a FORTRAN INTEGER*4, a 62-bit LISP
word, a Forth cell, a KRON array, a COBOL packed-decimal field, a wasm i32 in
linear memory. Babel gives them one value to meet in, the **Babel value**:

| Kind | Holds | Notes |
|---|---|---|
| integer | int64 | exact |
| ratio | num/den, den > 0, lowest terms | exact; zxapp_num_t already uses it |
| truth | NEITHER, TRUE, FALSE, BOTH | Belnap, as libzxv's ZXV_LP_* |
| bytes | a byte string | no encoding implied |
| text | UTF-8 | every other encoding is converted at the edge (section 4) |
| symbol | a name | case kept |
| list | ordered values | vectors, arrays, LISP and Logo lists |
| pair | a head and a tail that is not a list | LISP's dotted pair; (a . (b)) is the list (a b) |
| record | ordered named fields | Pascal records, COBOL group items, JSON objects |
| nothing | the empty value | a null; LISP's NIL is the empty list |

There are no floats, as everywhere in ZXV. A decimal that arrives at an edge
("1.25") becomes the exact ratio 5/4; one that cannot be exact (an IEEE
double with no short decimal) is refused, never rounded.

A Babel value has one canonical byte encoding (kinds and lengths spelled
out, records in field order, ratios in lowest terms). Two values are equal
exactly when their encodings are equal, so comparing the two sides of a
bridge is a byte comparison and can be signed and logged.

Freestanding C, integers only, a fixed arena per call, bounded nesting: a
value that does not fit traps LIMIT and is never truncated.

## 2. Value adapters (marshal in and out)

One adapter per runtime family, each a pair of functions (to Babel, from
Babel) with its refusals listed:

- **Lingua.** 62-bit words: pairs, vectors, strings, symbols, true, false
  and the empty list map to pair or list, list, text, symbol, truth and the
  empty list. A 32-bit program takes integers only. Printed output is read
  back by the language's own print syntax (kernel/src/babel/bv_syntax.c,
  built): `(1 2 . 3)` for LISP and Scheme, `[1 2 3]` for Logo, numbers for
  FORTRAN, BASIC, Pascal, ALGOL 60 and Forth; and a value is written back as
  a literal in that syntax. The tests read the reference tools' real output
  and compare one Collatz trace across eight languages.
- **Pillars.** Sutra records through the COBOL copybook layout
  (kernel/src/legacy/cobol.c); KRON arrays as lists; Eve and KRON truths;
  Trebago and MYRA through their libzxv bindings.
- **SDK languages** through libzxv's C ABI, which already carries integers,
  ratios and truths.
- **Z-Wasm.** i32 and i64, and lists or text read from linear memory at a
  declared offset and length.

A value that does not fit the target (a 64-bit integer into a 32-bit
FORTRAN INTEGER, text into a language with no strings) is refused by name.

## 3. Calling conventions

How a program receives its arguments and gives its answer differs by
language. Babel's call adapter states each one, so a block written in any
language can be called from any other:

- arguments in: READ or INPUT order, the Forth stack (first argument
  deepest), procedure parameters, wasm parameters, a COBOL LINKAGE record;
- answer out: printed values, the stack, a result value, a record;
- arrays: base 0 or 1, row or column major, carried in the adapter, never
  guessed;
- word size: 32, 62 or 64 bits, checked on the way in;
- parameters by value, by reference (copy in, copy out) and ALGOL's by name
  (refused across a bridge: it cannot be marshalled).

zxapp (kernel/src/apps) already calls every registered language with
integer vectors and compares witnesses. Babel widens those arguments and
answers from integer vectors to Babel values.

## 4. Formats and encodings

Babel registers the codecs ZXV already has instead of writing new ones:
EBCDIC (CCSID 037, 500, 1047), COBOL packed and zoned decimal, binary
fields and copybook records, GSM 7-bit and UCS-2
(kernel/src/legacy). Missing pieces it adds: CSV and fixed-width records,
and JSON restricted to exact numbers. An encoding is always declared; Babel
never sniffs one. Text that does not decode is refused with its offset.

## 5. Protocol shims

These exist already and Babel only catalogues them so a route can be
found: ISUP and SIP mapping (kernel/src/legacy/isup_map.c), the Web2, Web3
and Web4 resolver (kernel/src/bridge), the IRC and AIRC gateways, and
the Rosetta Shell's command dialects.

## 6. Every bridge is checked

- **Round trip.** a to Babel to a gives the same bytes, for every adapter
  and codec, over generated values.
- **Both sides.** Where a block exists in two languages, Babel runs it
  natively in each and compares the Babel values. Equal is TRUE.
  Different is BOTH, and the call is held, as zxapp's witnessed calls do
  today.
- **No silent fallback.** A missing adapter is a refusal naming the two
  ends, never a best guess.

## 7. Build order

1. The Babel value, its canonical encoding and its tests (built: bv.c).
2. Lingua adapters (print syntax built: bv_syntax.c) (62-bit values, and each built front-end's print syntax),
   then zxapp entries that take and answer Babel values.
3. The existing legacy codecs registered as Babel formats; CSV, fixed-width
   records, exact JSON.
4. Pillar adapters.
5. Z-Wasm and the SDK languages.
6. The bridge catalogue and a `babel check` run in CI that executes every
   bridge's round trip and both-sides comparison.

## 8. Limits

- Babel moves values between programs; it does not translate one program
  into another language. Each language's twin is its canonical spelling,
  and Rosetta explains code to people.
- No floats, no guessed encodings, no truncation.
