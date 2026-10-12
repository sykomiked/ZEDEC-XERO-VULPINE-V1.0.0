<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# apl-kron: exact array expressions for APL and K

This is the fourth legacy-language pillar. It is for people who write APL
or K and sometimes need an array expression whose answer is **exact**:
a sum over money, a matrix of rationals, a reference value for a test, a
comparison that must not be fooled by rounding. KRON is that expression
language. It reads a line of APL glyphs or of K-style ASCII, right to left
as both languages do, and evaluates it on vectors and matrices of
int64/int64 rationals and four-valued truth, every step through libzxv.

```text
APL     +/10⍴0.1                    1                (APL on doubles: 1, after rounding)
APL     x←⍳1000000 ⋄ +/x×x          333332833333500000   (a double sum gives 3.3333283333312755e17)
K       +/(!5)*1r2                  5
APL     ×/1+⍳21                     refused: ZXV_E_RANGE (21! does not fit int64), never rounded
APL     1 2 3<2                     (tvec true false false)
```

Both spellings and the canonical form are one program:

```text
APL         m←(+/x)÷≢x
K           m:(+/x)%#x
canonical   (let m (div (reduce add x) (tally x)))
```

`kr_program_digest` is the SHA3-256 of the canonical text, so
`examples/stats.apl` and `examples/stats.k` have the same digest
(`zkron same`), and `examples/fold.apl` and `examples/fold.k` do not:
APL's `-/` folds from the right and K's from the left, and KRON keeps the
meaning, not the glyph.

Pillar API 0.1 (`include/zkron.h`; it may change before 1.0) over the
libzxv ABI 1.3 ([ABI.md](../../docs/ABI.md)). The pillar status codes run
from -2301 to -2399.

## Why exact, and what it costs

Array languages compute on doubles. That is the right default: fast,
vectorised, and close enough for nearly everything. KRON is for the few
expressions where close is wrong.

- **It is slow.** Each element operation is a libzxv call with a 128-bit
  multiply and a gcd reduction, and KRON interprets a tree with no
  fusion or SIMD. On the development machine (x86-64, gcc 13, -O2),
  `x←⍳1000000 ⋄ +/x×x` took about 200 ms in `zkron` (including start-up
  and parsing) against about 1.5 ms for a C loop on doubles: two orders
  of magnitude. On fractions (`1000000⍴1r3 1r7 2`) it was about 300 ms.
  A GNU APL or ngn/k interpreter on doubles is in the C loop's range.
- **int64/int64 is the ceiling.** There are no big integers. `×/1+⍳20` is
  2432902008176640000; `×/1+⍳21` is refused with `ZXV_E_RANGE`. Long sums
  of unrelated fractions overflow their denominators quickly; sums of
  integers, or of values with a common denominator (cents), stay small.
- **No irrational functions.** No `*x` (exp), `⍟`, `○`, square roots or
  floats of any kind (R11). `x*n` needs an integer `n`.

**Use it** for reductions over money or counts, exact small linear
algebra, reference values for tests, checksums that must match across
machines, and results that will become ledger postings. **Do not use it**
for anything where a relative error of 1e-15 is fine, which is almost
everything.

## The language

A program is one or more statements, separated by newlines (or `⋄` in
APL, `;` in K). Assignment is `←` (APL) or `:` (K). The value of the
program is the value of its last statement. Names hold arrays only; there
are no user-defined functions. Free names (used but never assigned) are
bound by the caller (`kr_eval` bindings, or `zkron eval FILE x=(vec 1 2)`).

**Values.** Scalars, vectors and matrices (rank at most 2) of exact
rationals or of four-valued truth. Numbers are written `3`, `¯3` (APL),
`-3` (K), `0.25` (exactly 1/4), `1r3`. Exponent notation (`1e3`) is
refused: it names a binary approximation in both languages. Truth values
come from comparisons, or the words `true false neither both`.

**Evaluation.** Right to left, with no precedence, as in APL and K. KRON
is `⎕IO 0`. Scalar functions extend over arrays elementwise, a scalar
pairs with every element, and lengths must agree (`KR_E_LENGTH`).

| Canonical | APL | K | Meaning |
|---|---|---|---|
| `add sub mul div` | `+ - × ÷` | `+ - * %` | Exact arithmetic; `÷0` is `KR_E_DIVZERO` |
| `max min` | `⌈ ⌊` | `\| &` | On truth: four-valued or/and |
| `res pow` | `\|` and `*` | words only | Residue (APL sign rule); integer power |
| `neg sgn recip abs ceil floor` | `- × ÷ \| ⌈ ⌊` monadic | `-`, `_` (floor), words | |
| `lt le eq ge gt ne` | `< ≤ = ≥ > ≠` | `< = >`, words `le ge ne` | Give truth values |
| `and or not` | `∧ ∨ ~` | `~` (not), words | Truth only (libzxv's tables) |
| `match` | `≡`, `≢` (not match) | `~` | One truth value |
| `iota shape tally` | `⍳ ⍴ ≢` | `! #` (tally) | |
| `reshape cat catf` | `⍴ , ⍪` | `#`, `,` (first axis) | APL reshape cycles; `,` on matrices joins rows/columns |
| `take drop rotate` | `↑ ↓ ⌽` | `_` (drop), words | Negative counts work from the end (APL) |
| `rev revf flip ravel` | `⌽ ⊖ ⍉ ,` | `\|`, `+` | |
| `gup gdown sort` | `⍋ ⍒` | `< >`, word `sort` | Stable grades of numbers; truth has no order (`KR_E_DOMAIN`) |
| `find index` | `⍳`, `x[i]`, `x[i;j]` | `?`, `@` | |
| `reduce reducef foldl` | `f/ f⌿` | `f/` | APL folds from the right; K's over from the left |
| `scan scanf scanl` | `f\ f⍀` | `f\` | |
| `outer inner` | `∘.f`, `f.g` | `x f\:y` | |

Every canonical word can also be used as a function name in APL or K
text (`3 res 7`, `x le y`, `sort x`).

**K means K.** A K symbol keeps its K meaning or is refused; KRON never
quietly rereads it. Monadic `*` (first), `%` (sqrt), `&` (where), `=`
(group), `,` (enlist), `?` (distinct), `^` (null in ngn/k, sort in k9)
and dyadic `!`, `^` are refused with a reason. q's `<=`, `>=` and `<>`
are refused too, because in ngn/k `x<=y` is `x<(=y)`. For associative
functions a K left fold and an APL right fold are the same program
(`+/` has one digest); for `-/` and `÷/` they are different programs.

**Comments.** `⍝` in APL; in K, `/` at the start of a line or after a
space; in canonical text, `;` to the end of a line.

**Detection.** `KR_SPELL_AUTO` reads text that starts with the word
`kron` as canonical, text with any non-ASCII byte as APL, and anything
else as K. So **pure-ASCII APL such as `-/1 2 3` is read as K** under
AUTO. Pass `KR_SPELL_APL` (`zkron --apl`) when it matters; the GNU APL
binding always does.

A parse error or refusal reports line, column (in bytes), reason and
status:

```text
line 1, column 1: 3 × 8515157028618240000 does not fit int64/int64 exactly (refused, never rounded) [ZXV_E_RANGE]
```

**Status codes.**

| Code | Name | Class | When |
|---|---|---|---|
| -2301 | `KR_E_SYNTAX` | USAGE | the text does not parse, or uses a primitive KRON does not have |
| -2302 | `KR_E_NAME` | USAGE | an unbound or reserved name; a missing, duplicate or unknown binding |
| -2303 | `KR_E_RANK` | USAGE | wrong rank, or a result above rank 2 |
| -2304 | `KR_E_LENGTH` | USAGE | extents that do not agree |
| -2305 | `KR_E_DOMAIN` | USAGE | a value outside the function's domain: arithmetic on truth, `and` on numbers, a fractional count or exponent |
| -2306 | `KR_E_INDEX` | USAGE | an index outside its array |
| -2307 | `KR_E_DIVZERO` | USAGE | division by an exact zero (`÷`, `%`, `recip`, a literal `1r0`); `0\|x` is `x`, as in APL |
| -2308 | `KR_E_LIMIT` | REFUSED | the work limit or a size limit; a larger limit may succeed |
| -1..-99 | libzxv's | | `ZXV_E_RANGE` (does not fit), `ZXV_E_UTF8`, `ZXV_E_CAP`, `ZXV_E_FUNDS`, ... from `kr_post` |

## The C API (`include/zkron.h`)

| Call | |
|---|---|
| `kr_parse(text, len, spelling, &prog, &diag)` | Parse into an immutable program; `diag` gets line, column and message. |
| `kr_program_text`, `kr_program_digest`, `kr_program_spelling` | The canonical text (two-call buffer, R4) and its SHA3-256. |
| `kr_free_count`, `kr_free_at` | The names the caller must bind, in evaluation order. |
| `kr_eval(prog, bindings, count, work_limit, &value, &diag)` | Run; `work_limit` 0 means 2^32 element operations. |
| `kr_value_info`, `kr_value_rats`, `kr_value_truths`, `kr_value_text`, `kr_value_display`, `kr_value_make`, `kr_value_destroy` | Values: kind, rank, shape, elements; the canonical value text `(mat 2 2 1r2 1 2 3)`; rows for display. |
| `kr_run_text(text, len, spelling, what, buf, cap, &len)` | One call from text to value text, display, canonical program, digest, or `(shape;num;den)` in K syntax; a refusal writes its diagnostic. The bindings use it. |
| `kr_post(cap, amounts, from, to, ccy, ref_prefix, ids, ids_cap, &posted)` | Post a whole-number vector through `zxv_ledger_post_cap`, one posting per element, references `<prefix>-<i>`. Fractions are refused before anything is posted (`KR_E_DOMAIN`, never rounded); the ledger has no batch, so a refusal mid-way leaves `posted` postings standing and says which reference failed. |
| `kr_last_error`, `kr_last_work`, `kr_status_name`, `kr_status_class` | Per thread. |

`build/zkron` provides the same from a shell:
`zkron [--apl|--k|--canon] [--display] [--work N] eval|canon|digest|free FILE|-e TEXT`,
`zkron same A B` (digest comparison), `zkron check FILE...`, and
`zkron golden FILE...` for the golden files.

## Bindings

**ngn/k** (`bindings/k/`, tested). `zkron_k.c` is a `2:` extension; k
must be linked with `-Wl,-E` so the extension can find `Kc`/`KC`/`KE`.

```k
kronlib:"build/libzkron_k.so"; \l bindings/k/kron.k
kron "+/1r3 1r6"          / "1r2"
kronint "2 2#!4"          / (0 1;2 3)   when every element is whole
kronnd "1r3 2"            / (,2;1 2;3 1)  shape;numerators;denominators
.[kron;,"1%0";{x}]        / "KR_E_DIVZERO"; kronwhy[] has line, column, reason
```

ngn/k keeps 32 bytes of an error message, so a refusal is the status name
and `kronwhy[]` returns the full diagnostic. The test also runs 31
programs through ngn/k itself and checks KRON gives the same integers.

**GNU APL** (`bindings/apl/`, tested with GNU APL 2.0). A native function:

```apl
'build/libzkron_apl.so' ⎕FX 'KRON'
KRON '+/1r3 1r6 1r2'      ⍝ 1          integers, or booleans for true/false
0 KRON '+/1r3 1r6'        ⍝ 1r2        the value text
4 KRON '1r3 2' ⋄ 5 KRON '1r3 2'   ⍝ numerators 1 2, denominators 3 1
6 KRON '1÷0'              ⍝ the refusal as text, without raising
```

Monadic `KRON` gives integers only when every element is whole, else a
DOMAIN ERROR naming the element. Refusals become the matching APL error,
and `)MORE` shows KRON's diagnostic. The text is always read as APL. The
test runs 42 programs through GNU APL itself (`⎕IO←0`) and checks KRON
agrees.

**Dyalog APL and kdb+/q** should work through the same C ABI
(`kr_run_text` is the only call a binding needs: text in, text out), with
Dyalog's `⎕NA` or a q `2:` shim. **They are untested**: neither is open
source, and no binding for them is shipped.

Build the bindings with `make bindings NGNK=... NGNK_SRC=... GNUAPL=...
GNUAPL_SRC=...`. The k.h and GNU APL headers (AGPLv3 and GPLv3) are used
from those trees at build time and are not copied here.

## Honest limits

- **No big integers.** int64/int64, refused (`ZXV_E_RANGE`) when a
  result does not fit. Never rounded.
- **No floats, no irrational functions.** By design (R11).
- **Flat arrays, rank at most 2**, at most 2^22 elements per array. No
  nested arrays, strings, symbols, dictionaries, tables or user functions;
  no each, compress, replicate, dyadic transpose or `⍤`/`⍥`.
- **Not a dual-cone or symbolic system.** Truth is libzxv's four-valued
  logic (neither, true, false, both); there are no intervals, no cones,
  and no symbolic algebra.
- **Slow.** See above: two orders of magnitude behind doubles.
- **K is one dialect's meaning.** KRON follows ngn/k where K dialects
  disagree, and refuses what is ambiguous. `n#x` is APL reshape with
  cycling, so a negative count (take from the end in K) is refused.
- **Detection is a heuristic.** ASCII-only APL is read as K under AUTO.
- **Dyalog and kdb+ are untested.**

## Tests

| Suite | What | Count |
|---|---|---|
| `test_parse` | Accepted programs with exact canonical text in each spelling, refusals with line and column, APL/K digest equivalence and difference, detection, limits (depth 512, 1 MiB text, UTF-8), R4 buffers | 1,288 checks |
| `test_eval` | 6,000 random expressions compared with direct libzxv arithmetic, 400 random algebraic properties, values, bindings, `kr_run_text`, 4 threads | 88,396 checks |
| `test_post` | An exact payroll split posted to the ledger; refusals (fractions, zero, truth, matrices, read-only and revoked capabilities, duplicate references, funds mid-batch, a poisoned context) | 26 checks |
| golden (`zkron golden`) | 322 hand-written cases in `test/golden/*.kg` and 3,000 random programs, each in APL and K where both exist; expected values computed by `tools/gen_golden.py` with Python's `fractions` and libzxv's logic tables from `sdk/conformance/vectors.json`, never by libzkron. Every case also checks the canonical fixed point, digest stability and APL/K digest groups | 5,866 cases, 45,911 checks |
| `fuzz_parse` | Mutation fuzzer over 4 spellings: allowed statuses only, deterministic digest, canonical fixed point, the canonical program evaluates identically; also a libFuzzer entry point | 20,000 inputs per `make test`; 4 x 250k under ASan + UBSan in CI |
| examples | `stats` and `matrix` same in APL and K; `fold` must differ; `broken.kr` must fail | |
| `check-nofloat` | No float type in the C sources or bindings | |
| `bindings-k` | ngn/k: values, refusals, 31 programs compared with ngn/k | 40 checks |
| `bindings-apl` | GNU APL 2.0: values, errors, 42 programs compared with GNU APL | 42 checks |

The library also builds with `-mgeneral-regs-only` where supported, so a
floating-point instruction is a compile error. Everything builds with
`-Werror` under gcc 13 and clang 18. CI (`ci-jobs.yml`, to merge into
`.github/workflows/sdk-pillars.yml`) runs ubuntu-24.04 gcc and clang,
macos-14 clang, ASan + UBSan with both compilers, the fuzzer, libFuzzer,
and the bindings against ngn/k (pinned commit) and GNU APL 2.0.

## Quickstart

```sh
make -C sdk/libzxv all
make -C sdk/pillars/apl-kron test demo
sdk/pillars/apl-kron/build/zkron eval sdk/pillars/apl-kron/examples/stats.apl
sdk/pillars/apl-kron/build/zkron --k eval -e '+/x*x' 'x=(vec 1r2 1r3)'
```

## Layout

```text
include/zkron.h          the pillar API
src/                     lexer and parsers (APL, K, canonical), analysis,
                         printer, values, evaluator, text and posting bridge
tools/zkron_tool.c       the zkron command
tools/gen_golden.py      golden expectations from Python fractions
bindings/k/              ngn/k extension, kron.k, test
bindings/apl/            GNU APL native function, test
examples/                stats, matrix, fold (APL and K), broken.kr
test/                    C tests, golden sources, fuzzer
ci-jobs.yml              GitHub Actions jobs for this pillar
```

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.
