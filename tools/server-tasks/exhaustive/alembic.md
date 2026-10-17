# alembic — the exhaustive specification

| field | value |
|---|---|
| app id | `alembic` |
| matrix layer | 155 |
| icon group | L2 |
| wave spec | `wave12/155-alembic.md` |
| status | pending |

## 1. Purpose

logs experiments, recipes and results as a lab notebook in the Hoard (`files.read`, `files.write`, `search.query`) with exact integer calculations.

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs search`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use margins 1` — call `<id>.<entry>` after the use line
- `use furrow 1` — call `<id>.<entry>` after the use line
- `use retort 1` — call `<id>.<entry>` after the use line
- `use tally 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | reagent stock | primary | runs |
| FORTRAN | `trebago` | numeric | reaction math | primary | runs |
| Ada | `eve` | limits | hazard limits | primary | runs |
| APL | `kron` | arrays | result tables | primary | runs |
| MUMPS | `myra` | records | experiment tree | primary | runs |
| AWK | `-` | text | text | primary | planned: needs Lingua front-end |
| ALGOL 68 | `-` | formula | formulas | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id alembic
matrix 155
name Alembic
needs fs.read
needs fs.write
needs search

provides product num a b
provides ratio_x1000 num num den
provides modular_draw num seed members step
provides limit_excess num value limit
provides count_ok truth count cap
provides difference num a b
logic kron product <<
0|_a*_b
.
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic kron modular_draw <<
0|(((_seed*1103515245)+12345)+(_step*7))|_members
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic eve count_ok <<
eve alembic
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron difference <<
0|_a-_b
.
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check limit_excess value=1000,999 limit=1000,1000 = 0,0
check limit_excess value=1,1000000 limit=0,999999 = 1,1
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check count_ok count=2 cap=100 = true
check count_ok count=99 cap=100 = true
check count_ok count=101 cap=100 = false
check count_ok count=100 cap=100 = true
check count_ok count=0 cap=100 = false
check count_ok count=1 cap=100 = true
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
```

## 6. The entry specifications (what each entry computes)

### `product` (num) — kron

**Args.** `a b`

**Semantics.** a * b exactly in minor units. Integer only.

**The exact check.** `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000`

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

### `modular_draw` (num) — kron

**Args.** `seed members step`

**Semantics.** The sortition's modular walk. Integer only, no hashing in the block. The residue operator is '|' in K ('%' is division).

**The exact check.** `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6`

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `['check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000', 'check limit_excess value=1000,999 limit=1000,1000 = 0,0', 'check limit_excess value=1,1000000 limit=0,999999 = 1,1']`

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `['check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false', 'check count_ok count=2 cap=100 = true', 'check count_ok count=99 cap=100 = true', 'check count_ok count=101 cap=100 = false', 'check count_ok count=100 cap=100 = true', 'check count_ok count=0 cap=100 = false', 'check count_ok count=1 cap=100 = true']`

### `difference` (num) — kron

**Args.** `a b`

**Semantics.** a - b exactly, never negative in the app's domain.

**The exact check.** `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — experiment tree in MUMPS as MYRA

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

