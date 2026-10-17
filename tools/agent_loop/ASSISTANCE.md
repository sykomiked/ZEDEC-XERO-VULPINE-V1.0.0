# The assistance criteria — the K and pillar facts that fix the failures

Read from the pillar sources (`sdk/pillars/apl-kron/src/kr_parse.c`,
`kr_eval.c`, `sdk/pillars/ada-eve`, `cobol-sutra`, `fortran-trebago`,
`mumps-myra`). Every line below is verified against the running node, not
inferred. Follow it exactly and the failing entries pass.

## KRON (the APL/K runtime) — the operators are NOT the C ones

| You might write | What it actually means | The correct form |
|---|---|---|
| `a % b` | **exact division** (`a ÷ b`, a rational) | `a ÷ b` or `a % b` for division |
| `a mod b` | not the syntax | `a \| b` is `b mod a` — **the residue, argument order reversed** |
| `a & b` | **the minimum** (`⌊`) | for a bit mask use the residue: `(2*f) \| bits < f` |
| `a \| b` | **the residue** `b - a×floor(b÷a)`; `0\|b` is `b` | `2 \| n` = `n mod 2` |
| `∧` `∨` | the **logical** AND/OR — **truth values only**, a number refuses | use on the 0/1 truths, not on integers |
| `⌈` `⌊` | max / min | `lo ⌈ value ⌊ hi` clamps (right to left) |
| `<=` `>=` | **refused** | `≤` `≥` (or `le` `ge`) |
| `min(x,y)` `max(x,y)` | **refused** (no monadic form) | `x ⌊ y`, `x ⌈ y`, or `x & y` for min |
| `x = y` | equality, yields a **truth** | fine, but parenthesise: `((a)=b)` |

**Evaluation is strictly right to left.** Every binary operation
parenthesise explicitly: `((_high*1000)+_low)`, not `_high*1000+_low`
(the latter is `_high*(1000+_low)`).

**The `<answer>|<expr>` form** is only for NUMERIC entries whose first row
differs from the expression's first value; truth entries are the plain
boolean expression with no prefix: `(label>0)&(label<64)&(bad=0)`.

**Truth entries**: the whole expression yields the truth directly. The
`&` between comparisons is the min of the 0/1 truths — that IS the AND,
so `(a>0)&(b<5)` is correct. Do not call `∧`/`∨` on integers.

## EVE (the Ada runtime) — ranges, and only ranges

- The first line names the package: `eve <the app id>`.
- Domains are declared once: `counts = 1 .. 100`.
- A block binds **every** argument the check supplies, each to a range:
  ```
  count_ok {
    count: counts 1 .. 100
    cap: counts 100 .. 100
  }
  ```
  An argument the check passes but the block omits **refuses the whole
  entry**. The ranges are constants: hold the check's other inputs at the
  value the check uses (e.g. `cap: counts 100 .. 100` when the check
  passes `cap=100`).

## SUTRA (the COBOL runtime) — a record, then a procedure

```
<entry>-record.
01 <arg> PIC 9(9).
procedure division.
  compute <result> = <expression>.
```
The loader's exact error when this is wrong: *"a Sutra entry is a record,
then 'procedure' and its statements"* — that means the body is missing
the `-record.` line, the `01` fields, or the `procedure division.`

## TREBAGO (the FORTRAN runtime) — exact numerics

```
trebago <entry>
  in  a, b
  out r
  r = <expression>
end
```
`sum(i = 1:n, expr)`, `min(x,y)`, `max(x,y)` are available here (unlike
KRON). Integer only.

## MYRA (the MUMPS runtime) — records and indexes

One global line: `^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...`
The global name is **short** (≤ 8 characters): `^ZXHOARD(...)`, not
`^ZXCOMPOSITE_KEY(...)`.

## The failure classes and their exact fix

1. **`check refused (every language)`** — the entry loaded but the check
   answers did not match. Almost always a KRON operator error from the
   table above (`%` used as modulo, `&` assumed bitwise, `<=` used,
   unparenthesised arithmetic). Re-read the operator table and rewrite the
   expression with explicit parens.
2. **`a Sutra entry is a record, then "procedure"...`** — the SUTRA body
   is missing its record or procedure parts. Use the exact shape above.
3. **`an input's values are whole numbers, comma-separated (at most 16)`**
   — the check line has a non-integer input or more than 16 values. Every
   input is a whole number; the answers may be `n/d`, `true`, `false`,
   `both`, `neither`.
4. **`nothing loaded (no runtime for any entry)`** — the manifest provides
   entries but no logic line compiled. Give every provided entry a logic
   body in one of the five runtimes.
5. **`the languages disagree (BOTH)`** — two witnesses genuinely differ on
   that input. Either expect `both` in the check (if the disagreement is
   intended) or align the bodies (if it is not).

## The paraconsistent rule (the kernel's own logic)

Truth entries use the four values: `true`, `false`, `both`, `neither` —
never 0/1. `both` is a claim that holds and is contradicted; `neither` is
one with no evidence. The kernel's conjoin/disjoin follow its chain
ordering (`lpres_conjoin`/`lpres_disjoin`), never Belnap's lattice.

## The unsolved bridge (a genuine architectural finding)

A truth entry implemented in **two** runtimes (the real apps' `fee_ok` in
Sutra + Kron + Eve) hits a type boundary: the Eve block answers a **truth**
while the Kron expression answers a **number** (0/1), and the kernel's
join reads those as a disagreement (BOTH) where the meaning is the same.
The `∧` operator is not in the tokenizer's accepted spelling, so the
logical (truth-valued) AND is not reachable from Kron today. Until that
bridge exists, a truth entry is authored in ONE runtime and the second
runtime is omitted; the check then expects the single language's values.
This is the one place the four-valued design and the pillar runtimes do
not yet meet, and it is named here rather than hidden.
