# apothecary — the exhaustive specification

| field | value |
|---|---|
| app id | `apothecary` |
| matrix layer | 180 |
| icon group | L2 |
| wave spec | `wave14/180-apothecary.md` |
| status | pending |

## 1. Purpose

keeps clinical records and prescriptions in the Hoard (`files.read`, `files.write`) with signed approvals and medical ID. A clinician signs an approval with a passkey (the passkeys methods, under sys.read, added for this). Anything else missing: use only existing methods; list it in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs notify`
- `needs sys.read`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use lockbox 1` — call `<id>.<entry>` after the use line
- `use sealwax 1` — call `<id>.<entry>` after the use line
- `use wellspring 1` — call `<id>.<entry>` after the use line
- `use watchfire 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | billing codes | primary | runs |
| FORTRAN | `trebago` | numeric | dose calculation | primary | runs |
| Ada | `eve` | limits | prescribing limits | primary | runs |
| APL | `kron` | arrays | lab result grid | primary | runs |
| MUMPS | `myra` | records | patient record tree | primary | runs |
| Haskell | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |
| Tcl | `-` | scripting | scripting | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id apothecary
matrix 180
name Apothecary
needs fs.read
needs fs.write
needs notify
needs sys.read

provides limit_excess num value limit
provides count_ok truth count cap
provides ratio_x1000 num num den
provides exact_total num n a b c d
provides product num a b
provides composite_key num high low
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic eve count_ok <<
eve apothecary
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
logic kron product <<
0|_a*_b
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## 6. The entry specifications (what each entry computes)

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

### `exact_total` (num) — trebago

**Args.** `n a b c d`

**Semantics.** The exact sum of the first n of the four sizes. Integer only, no rounding, no overflow silently: the caller checks the bound.

**The exact check.** `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0`

### `product` (num) — kron

**Args.** `a b`

**Semantics.** a * b exactly in minor units. Integer only.

**The exact check.** `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — lab result grid in APL as KRON

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

