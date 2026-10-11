<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# mumps-myra: M globals and MYRA over libzxv

The fifth legacy-language pillar. In M (MUMPS), data lives in **globals**:
sparse, sorted trees such as `^ZXPAY(7,1)="PAY-0001^250.25^NGN^1^2^67854^N"`.
The subscripts are the key and the value is a string whose fields are
`^`-delimited pieces. What each subscript and piece means is written down
somewhere else. libzmyra reads that declaration and holds the data to it
**exactly**.

- **The global map is the treaty.** A short, documented line format says
  which nodes a global has, what type each subscript is, and what each
  piece holds: text up to n bytes, an integer of n digits, an exact
  decimal `NUMBER(p,s)`, or a date as an M `$H` day count.
- **MYRA is the short form.** The same schema in one line per node. A
  global map and a MYRA file that declare the same schema compile to the
  identical canonical schema, with the same SHA3-256 digest.
- **Values follow M's own number rules.** A piece must be a number in M
  canonical form (`12.5`, `.5`, `-.25`; never `12.50`, `0.5`, `+1` or
  `1E3`) and must fit its field exactly. Anything else is refused, never
  rounded. Numbers are an `int64` plus a scale, or a libzxv rational.
  There are no floats.
- **M collation.** `zm_collate` orders subscripts the way M does: canonical
  numbers first in numeric order, then strings in byte order. It agrees
  with GT.M's own `$ORDER`; `make m` checks this.
- **Postings go through a capability.** A payment node becomes one ledger
  posting through `zxv_ledger_post_cap`, with a capability you pass in.
  Balancing (A = L + E) is the ledger's job, and its refusals come back as
  status codes.
- **An M binding that has been run.** `m/ZMYRA.m` and an external-call
  table let M code call libzmyra. `make m` runs it on GT.M against a real
  database.

```text
; payment.gmap                                ; payment.myra
GLOBAL ^ZXPAY                                 ^ZXPAY(batch:i9, seq:i9) = ref:t16! ^ amount:n15.2!
NODE ^ZXPAY(BATCH,SEQ)                          ^ ccy:t3! ^ from:i10! ^ to:i10! ^ value-date:d
  KEY   BATCH        INTEGER(9)                 ^ status:t1 ^ ^ memo:t40
  KEY   SEQ          INTEGER(9)               ^ZXPAY(batch:i9) = opened:d! ^ count:i6
  PIECE 1 REF        TEXT(16)       REQUIRED    ^ total:n18.2 ^ name:t30
  PIECE 2 AMOUNT     NUMBER(15,2)   REQUIRED  ^ZXPAY("B", ref:t16, batch:i9, seq:i9) =
  PIECE 3 CCY        TEXT(3)        REQUIRED
  ...                                         (each MYRA node is one line in the file)
NODE ^ZXPAY(BATCH)
  ...
NODE ^ZXPAY("B",REF,BATCH,SEQ)
  KEY   REF          TEXT(16)
  ...
```

```text
$ zmyra same examples/payment.gmap examples/payment.myra
same schema: examples/payment.gmap and examples/payment.myra, sha3-256 3fc2a11adbe2b2ac...
```

Pillar API 0.1 (`include/zmyra.h`). It may still change before 1.0. It
calls only the libzxv ABI 1.3 (`sdk/libzxv/include/zxv_api.h`):
[ABI.md](../../docs/ABI.md). Status codes are -2401 to -2499.

> **Not a clinical or regulated-system component.** Most M code in
> production runs health systems. This pillar makes **no** HIPAA,
> clinical-safety, medical-device or certification claim. It checks the
> shape of data, not what the data means. Any clinical use is advisory
> only and needs a qualified human's approval. Every example here uses
> made-up data (payments and stock), and nothing in this folder is or
> resembles a patient record.

## Quickstart

```sh
make -C sdk/libzxv all                      # the C library every SDK wraps
make -C sdk/pillars/mumps-myra test demo    # tests, then the demo
sdk/pillars/mumps-myra/build/zmyra decode sdk/pillars/mumps-myra/examples/payment.myra \
    '^ZXPAY(7,1)' 'FUND-0001^1000^NGN^1^2^67854^N'
```

With GT.M installed (`apt-get install fis-gtm` on Ubuntu/Debian):

```sh
make -C sdk/pillars/mumps-myra m           # the M binding and the GT.M cross-checks
```

The demo:

```text
examples/payment.gmap and examples/payment.myra declare the same schema: sha3-256 3fc2a11a...
^ZXPAY(7,2) amount 250.25 = 25025 / 10^2 = 1001/4, value-date $H 67854 = 2026-10-11
1/3 into amount: ZM_E_FIT (never rounded)
0250.50 into amount, M canonical value: PAY-0001^250.5^NGN^1^2^67854^N
"12.50" in a value string: ZM_E_DATA: piece 2 (amount): "12.50" is not a number in M canonical form
post ^ZXPAY(7,1)  FUND-ALICE^1000^NGN^0^1^67854^N -> entry 1 (100000 minor units)
post ^ZXPAY(7,2)  PAY-0001^250.25^NGN^1^2^67854^N -> entry 2 (25025 minor units)
post ^ZXPAY(7,3)  PAY-0002^5000^NGN^1^2^67854^N -> ZXV_E_FUNDS
post ^ZXPAY(7,4)  PAY-0001^1^NGN^1^2^67854^N   -> ZXV_E_DUPLICATE
post ^ZXPAY(7,5)  PAY-0003^1.005^NGN^1^2^67854^N -> ZM_E_FIT
post ^ZXPAY(7,6)  PAY-0004^12.34^USD^1^2^67854^N -> ZXV_E_CURRENCY
equity in kobo: bank -100000, alice 74975, bob 25025 (sum 0)
ledger check: ZXV_OK
```

## How M values map to exact values

M has one data type, the string. A string is a **number in canonical
form** when M's unary plus gives it back unchanged (`x=+x`). M itself
writes every computed number that way, so `set $piece(v,"^",2)=12.50`
stores `12.5`. libzmyra holds pieces to exactly that rule, as GT.M and
YottaDB apply it:

| Canonical | Not canonical |
|---|---|
| `0` `7` `-7` `12.5` `.5` `-.25` | `00` `01` `0.5` `-0.5` `12.50` `1.` `-0` |
| `123456789012345678` (18 significant digits) | `1234567890123456789` (19) |
| `1000000000000000000000` (one significant digit) | `1E21`, `1e3`, `+1`, `" 1"`, `"1 "` |
| `10000000000000000000000000000000000000000000000` (1E46) | 1E47 and above (GT.M overflows) |
| `.0000000000000000000000000000000000000000001` (1E-43) | anything below 1E-43 |

`test/golden/canonic.m-expect` is GT.M's own verdict on 54 such strings,
and `make m` compares 200,000 generated number-like strings with GT.M.

| Field type | Global map | MYRA | A piece holds | In C |
|---|---|---|---|---|
| text | `TEXT(n)` | `tN` | 0..n bytes of printable UTF-8, no `^`, no control characters | the bytes |
| integer | `INTEGER(n)` | `iN` | a canonical integer of at most n digits (n <= 18) | `int64` |
| exact decimal | `NUMBER(p,s)` | `nP.S` | a canonical number with at most p digits, s after the point | `int64` unscaled + scale s, or a rational |
| date | `DATE` | `d` | a `$H` day count 0..2980013 (1840-12-31..9999-12-31) | `int64` day; `YYYY-MM-DD` as text |

`NUMBER(p,0)` is the same as `INTEGER(p)` (it prints as `INTEGER(p)`).
An empty piece is null. `REQUIRED` (`!` in MYRA) makes an empty piece
an error. Keys are always required.

**What is refused, and how you find out:**

| You do | You get |
|---|---|
| decode `12.50`, `0.5`, `+1`, `1E3` or `01` in a numeric piece | `ZM_E_DATA`: not a number in M canonical form |
| decode `1.005` into `n15.2`, 14 integer digits into `n15.2`, `1.5` into an integer, day 2980014 | `ZM_E_FIT`, never rounded |
| decode 17 bytes into `t16`, a control byte, invalid UTF-8 | `ZM_E_FIT` / `ZM_E_DATA` |
| decode a value with a non-empty piece the schema does not declare | `ZM_E_DATA` |
| leave a `REQUIRED` piece empty | `ZM_E_DATA` |
| put `1/3`, or `1.234` into two decimals | `ZM_E_FIT`, nothing written |
| put `1.230` into two decimals, or `-0012.50` | accepted: nothing is lost |
| put `a^b` into a text piece | `ZM_E_DATA` (it would split the piece) |
| a reference like `^ZXPAY(7,01)` or `^ZXPAY(7,"X")` | `ZM_E_KEY` |

Every refusal names the field and the piece in `zm_last_error`.

## The global map

```text
map      = "GLOBAL" "^" gname { node }
node     = "NODE" "^" gname [ "(" sub { "," sub } ")" ] { key | piece }
sub      = NAME | literal              (each NAME needs one KEY line)
key      = "KEY" NAME type
piece    = "PIECE" number NAME type [ "REQUIRED" ]
type     = "TEXT(" n ")" | "INTEGER(" n ")" | "NUMBER(" p "," s ")" | "DATE"
literal  = canonical-number | '"' text '"'      ("" inside a string is one quote)
comment  = ";" to the end of the line
```

- One statement per line; indentation is free. Keywords and field names
  are not case-sensitive. **The global name is**: `^ZXPAY` and `^zxpay`
  are two globals in M, and here too.
- A map declares one global and any number of nodes (up to 256). A node
  has up to 31 subscripts (GT.M's limit) and pieces numbered 1..255, in
  any order, with gaps allowed. A node with no pieces is an index node:
  its value must be empty (`^ZXPAY("B",REF,BATCH,SEQ)=""`).
- Literal subscripts are written as M prints them: `0`, `-2.5`, `.5`,
  `"B"`. A quoted canonical number is that number, as in M (`"10"` is
  `10`).
- **No two nodes may match the same reference.** `^X(ID)` with an integer
  key and `^X(D)` with a date key are refused, because `^X(5)` would fit
  both. A literal or a text key against a numeric key keeps them apart.

## MYRA

```text
myra   = { node-line }
line   = "^" gname [ "(" sub { "," sub } ")" ] "=" [ piece { "^" piece } ]
sub    = name ":" type | literal
piece  = [ name ":" type [ "!" ] ]       (an empty piece is an unused position)
type   = "t" n | "i" n | "n" p "." s | "d"
```

Spaces and tabs are free around tokens, `;` starts a comment, and names
and type letters are not case-sensitive. `^ZXSTK(site:t8,item:i12,"PRICE",effective:d)=price:n12.2!^^ccy:t3!`
is a node with two text and date keys around a literal, a required price
in piece 1, an unused piece 2 and a required currency in piece 3.

The canonical form of any schema is MYRA text (`zm_schema_myra`, or
`zmyra schema FILE`): one line per node in declaration order, lower-case
names, no spaces, no comments, no trailing empty pieces. The digest is the
SHA3-256 of that text. `zm_schema_gmap` prints the same schema as a global
map. Both print forms parse back to the same digest. The tests and the
fuzzer check this.

| Refused | Status |
|---|---|
| `SET` / `s`, `POINTER` / `p`, `WP` / `w`, `DATETIME` / `h` | `ZM_E_UNSUPPORTED`: not in this subset (README "Honest limits") |
| `FLOAT`, `REAL`, `DOUBLE` / `f`, `r` | `ZM_E_UNSUPPORTED`: floating point (R11) |
| a second global in one file | `ZM_E_UNSUPPORTED` |
| two nodes that can match one reference, a key without a subscript, a subscript without a key, a name or piece used twice, `01` as a literal, `""` as a literal, `!`/`REQUIRED` on a key | `ZM_E_SYNTAX` |

Every refusal names the line and column:

```text
examples/broken.myra:5:1: this node and node 1 (line 4) can match the same reference; make a literal subscript or a key type differ [ZM_E_SYNTAX]
^ZXBAD(id:i9,day:d)=note:t20
^
```

## Collation and references

M keeps every global sorted. `zm_collate(a, alen, b, blen, &cmp)` gives
that order for two subscripts:

1. the empty subscript (where null subscripts are allowed);
2. canonical numbers, in numeric order. They are compared exactly as
   decimal text, so `.000000000000000001` < `.0000000000000000001` is
   decided digit by digit, never through a float;
3. every other string, in byte order (a prefix first).

So `-10 < -1 < .5 < 2 < 10 < "01" < "1.0" < "10A" < "A" < "a"`.
`zm_ref_collate` orders whole references the way `$QUERY` walks them: a
parent before its children, then subscript by subscript.

A reference is written the way `$QUERY` and `ZWRITE` print it:
`^ZXPAY(7,1)`, `^ZXPAY("B","say ""hi""",7,2)`. `zm_node_match` finds the
node it belongs to, and `zm_node_decode` matches, sets the keys and decodes
the value in one call.

## Using it from C

```c
#include "zmyra.h"

zm_schema *s;  zm_diag d;
if (zm_schema_parse(text, len, &s, &d) != ZXV_OK)      /* a global map or MYRA */
    fprintf(stderr, "%s\n", d.msg);                    /* "line 5, column 1: ..." */

zm_record *r;
zm_node_decode(s, "^ZXPAY(7,2)", 11, value, value_len, &r);
int64_t units;  uint32_t scale;
zm_get_decimal(r, "amount", &units, &scale);          /* 25025, 2 */
zm_get_value(r, "value-date", buf, sizeof buf, &n);   /* "2026-10-11" */
zm_put_value(r, "amount", "0250.50", 7);              /* stored as 250.5 */
zm_put_rat(r, "amount", 1, 3);                        /* ZM_E_FIT */
zm_record_encode(r, buf, sizeof buf, &n);             /* the canonical value string */
zm_record_destroy(r);
zm_schema_destroy(s);
```

Status codes are libzxv's (`ZXV_E_*`, passed through) plus five of the
pillar's own: `ZM_E_SYNTAX` (-2401), `ZM_E_UNSUPPORTED` (-2402),
`ZM_E_DATA` (-2403), `ZM_E_FIT` (-2404) and `ZM_E_KEY` (-2405). Text
outputs use libzxv's two-call buffers (R4). A failed call writes nothing.

## The posting bridge

```c
zm_posting_map map = {
    .amount = "amount", .from = "from", .to = "to",
    .currency = "ccy", .reference = "ref",   /* NULL: the node's own reference */
    .minor_units = -1,                       /* from ISO 4217 */
};
zxv_cap *poster;                             /* LEDGER_POST | LEDGER_READ, nothing else */
zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &poster);
zm_posting p;
int32_t st = zm_post(poster, r, &map, &p);
```

`zm_post` reads the amount, converts it exactly to the currency's minor
units, reads the two accounts (any integer field or key, a whole number in
0..2^32-1) and the reference (at most 35 bytes), and calls
`zxv_ledger_post_cap`. `zm_posting_read` does the same without posting.

| Status | Meaning |
|---|---|
| `ZXV_OK` | posted; `p.entry_id` is the ledger's entry |
| `ZM_E_FIT` | 12.5 JPY (yen has no decimals), 1.005 NGN, account 1.5 or 2^32, a reference over 35 bytes. Never rounded. |
| `ZXV_E_FIELD` | a mapped field is empty |
| `ZXV_E_CAP` | the capability is missing `LEDGER_POST`, revoked or unattested |
| `ZXV_E_FUNDS` / `ZXV_E_DUPLICATE` / `ZXV_E_RANGE` | a holder would go below zero / the reference was used / the amount is zero, negative or too large |
| `ZXV_E_CURRENCY` / `ZXV_E_NOT_FOUND` | no implicit FX, or a code ISO 4217 does not know / an unknown account |
| `ZXV_E_POISONED` | the context halted; FATAL, never retry |

Inside one process a capability is least-privilege discipline, not a
sandbox ([ABI.md R10](../../docs/ABI.md#r10-capabilities-new-in-11)).

## The M binding (GT.M, YottaDB)

`m/zmyra_xc.c` is a call-out library and `m/zmyra.xc.in` its
external-call table. Strings cross as `gtm_string_t`, so values are passed
exactly. `m/ZMYRA.m` wraps the calls as extrinsics:

```mumps
 set h=$$LOAD^ZMYRA("examples/payment.myra")      ; a handle, or a negative status
 set st=$$CHECK^ZMYRA(h,"^ZXPAY(7,1)")            ; 0, or e.g. ZM_E_DATA
 set st=$$GET^ZMYRA(h,"^ZXPAY(7,2)","amount",.v)  ; v="250.25"
 set st=$$PUT^ZMYRA(h,"^ZXPAY(7,1)","status","P") ; stores the canonical value
 set st=$$POST^ZMYRA(h,"^ZXPAY(7,2)","amount,from,to,ccy,ref",.entry)
 write $$STNAME^ZMYRA(st),": ",$$ERR^ZMYRA(),!
```

`make m` builds `build/m/libzmyra_xc.so`, writes `build/m/zmyra.xc`,
creates a fresh database in `build/m/`, and runs `m/ZMYRATST.m` with
`mumps`. That routine sets a batch of synthetic payments in a real global
`^ZXPAY`, walks it with `$QUERY`, checks every node with both the MYRA
schema and the global map, posts the valid payments to a libzxv ledger,
and revokes the capability. Its output must equal `m/ZMYRATST.expected`:

```text
^ZXPAY(7,4) node 1 ZXV_OK
  post ZXV_E_DUPLICATE
^ZXPAY(7,5) node 1 ZM_E_DATA: piece 2 (amount): "12.50" is not a number in M canonical form
^ZXPAY(7,6) node 1 ZM_E_FIT: piece 2 (amount): 1.005 has more than 2 decimals (never rounded)
^ZXPAY(7,"X") node - ZM_E_KEY: ^ZXPAY(7,"X") matches no node of ^ZXPAY: subscript 2: subscript "X" is a string; key seq is a number
...
after +$piece: PAY-0003^12.5^NGN^1^2^67854^N ZXV_OK
post after revoke: ZXV_E_CAP
```

Then `make m` asks GT.M itself (`m/ZMYRAGLD.m`): its `$ORDER` of
`test/golden/collate.in`, its `x=+x` on `test/golden/canonic.in` and its
`$ZDATE` on chosen `$H` days must equal the checked-in
`test/golden/*.m-expect` files, which `make test` holds libzmyra to on
machines without M. Finally a cross-check compares GT.M and libzmyra on
200,000 generated number-like strings, 100,000 generated subscripts in
`$ORDER` order, and every `$H` day from 0 to 2980013:

```text
canonic: 200000 strings, 74871 of them canonical numbers
collate: 75180 distinct subscripts in M order
dates: 2980014 days
cross-check: 0 differences
```

Tested with GT.M V7.0-005 (Ubuntu 24.04's `fis-gtm`). The table and
Makefile also set YottaDB's variables (`ydb_xc_zmyra`, `ydb_routines`,
`ydb_gbldir`) and YottaDB reads GT.M-style tables, but **YottaDB itself
has not been run here**. `GTM_DIST=...` picks the install.

## Tests

`make test` runs:

| Test | Checks | What |
|---|---|---|
| `test_number` | 228,141 | canonical-number vectors (GT.M's limits), exact decimal text both ways, a seeded round trip at every scale 0..18, and every `$H` day 0..2980013 both ways |
| `test_schema` | 497 | global map and MYRA give identical schemas (digest, text, every field), 7 spellings of one schema, printer round trips, limits, 70 refusals each at its line and column |
| `test_record` | 94,724 | 61 decode vectors (canonical forms, fits, text, required and undeclared pieces, references), getters and setters, keys from references, and a seeded round trip over 400 random schemas |
| `test_collate` | 303,014 | a hand-ordered list (every pair), 200,000 number pairs against exact integer arithmetic, order laws on 100,000 random triples, `$QUERY` order of references |
| `test_golden` | 1,582 | GT.M's own collation, canonical numbers and dates; pinned schema digests; 36 golden nodes decoded with both forms of their schema |
| `test_post` | 54 | postings and every refusal status, minor units, private units, a poisoned context |
| `fuzz_parse` | 3,000 inputs | the smoke run (see below) |

That is 628,012 checks. Plus the R11 check (`check-nofloat`; the sources
are also compiled with `-mgeneral-regs-only` where the compiler supports
it), `zmyra same` on both example pairs, the two broken examples (which
must be refused), and `zmyra collate` against GT.M's order.

The fuzzer feeds every input to the MYRA parser, the global-map parser
and the auto-detecting one. It checks determinism and error positions,
that the canonical MYRA and the printed map parse back to the same digest,
and decodes values cut from the input on every node of every accepted
schema: a decoded value must re-encode to a stable canonical string whose
numeric pieces are all canonical in M. It also checks the collation laws
and references on the raw input. `make fuzz FUZZ_ITERS=250000` runs it
longer; CI runs 4 x 250,000 inputs under ASan and UBSan, and libFuzzer for
60 seconds where clang ships it. The fuzzer found one bug during
development (an overflow quoting a long run of control bytes in an error
message); `test_record` keeps a case for it.

```sh
make test                     # everything above
make SANITIZE=1 OUT=build/san test
make fuzz FUZZ_ITERS=250000   # ZM_FUZZ_SEED=n for another stream
make m                        # needs GT.M (or YottaDB, untested)
make golden-m                 # rewrite test/golden/*.m-expect from the M engine
```

CI: the jobs in [`ci-jobs.yml`](ci-jobs.yml) (ubuntu-24.04 gcc and clang,
ASan + UBSan with both, macos-14 clang, GT.M on ubuntu-24.04).

## Honest limits

- **Not a clinical, regulated or certified component.** No HIPAA,
  clinical-safety, FDA or ONC claim of any kind. Use it to check data
  shapes; any clinical use is advisory only, with human approval.
- **Not FileMan.** VA FileMan data dictionaries (`^DD`), its internal
  dates (`3261011`), set-of-codes, pointer, computed, multiple and
  word-processing fields are not read. The global map is this pillar's
  own format. Describe a FileMan file in it by hand if you need to.
- **Dates only.** `$H` times (`67854,43200`), time zones and `$ZDATE`
  formats other than ISO are not supported.
- **Data only.** libzmyra does not open databases, take locks, run
  transactions or replicate. Your M code (or an M engine) reads and writes
  globals; libzmyra interprets one node's value at a time.
- **Sizes are not the database's.** Values may be up to 1 MiB (GT.M's
  string limit). The real limits on key and record size are set per
  database (`KEY_SIZE`, `RECORD_SIZE`) and are not checked here.
- **Printable text only.** Control characters in text pieces and string
  subscripts are refused, so data that stores them (`$C(9)`) is refused.
  No `$ZCHSET`/UTF-8 mode distinction: text is checked as UTF-8 bytes.
- **Null subscripts**: `zm_collate` puts `""` first, as GT.M does when null
  subscripts are allowed; references with `""` are refused because
  databases refuse them by default.
- **The M glue is an example binding.** It keeps one ledger context and
  16 schema handles per process, and posts with one attenuated capability.
  It is tested on GT.M V7.0-005 only; YottaDB, InterSystems IRIS/Caché and
  other M engines have not been run.
- **Capabilities are discipline in-process.** Enforcement needs a process
  boundary (`zxvd`).

## Files

| Path | What |
|---|---|
| `include/zmyra.h` | the pillar API |
| `src/zm_gmap.c`, `src/zm_myra.c`, `src/zm_lex.c` | the two front ends and their lexer |
| `src/zm_schema.c` | validation, canonical MYRA, the global-map printer, digest, accessors |
| `src/zm_num.c` | canonical numbers, collation, exact decimals, `$H` dates |
| `src/zm_ref.c`, `src/zm_record.c` | references; value strings <-> typed fields |
| `src/zm_post.c` | the posting bridge |
| `tools/zmyra_tool.c` | `zmyra schema / gmap / fields / same / decode / collate / canonic` |
| `examples/` | `payment` and `stock` as global map and MYRA, two broken files, the demo |
| `m/` | `ZMYRA.m` (wrappers), `ZMYRATST.m` and its expected output, `ZMYRAGLD.m` (GT.M golden files and cross-check), `zmyra_xc.c` and `zmyra.xc.in` |
| `test/` | the tests, the fuzzer, `golden/` (files written by GT.M, pinned digests, golden nodes) |
| `ci-jobs.yml` | the GitHub Actions jobs for this pillar |

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.
