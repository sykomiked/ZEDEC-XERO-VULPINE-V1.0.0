# collet — the exhaustive specification

| field | value |
|---|---|
| app id | `collet` |
| matrix layer | 309 |
| icon group | L3 |
| wave spec | `wave26/309-collet.md` |
| status | pending |

## 1. Purpose

locks sensitive data in escrow that opens only under agreed rules, such as two keyholders or a set date (`recovery.setup`, `recovery.status`, `recovery.approve`, `recovery.veto`, `files.cross`, `journal.query`).

## 2. Capabilities (the exact registry names and scopes)

- `needs peers`
- `needs journal`
- `needs notify`
- `needs fs.read`
- `needs fs.write`
- `needs sys.read`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use warding 1` — call `<id>.<entry>` after the use line
- `use pavise 1` — call `<id>.<entry>` after the use line
- `use cartulary 1` — call `<id>.<entry>` after the use line
- `use lockbox 1` — call `<id>.<entry>` after the use line
- `use heirloom 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | escrow fees | primary | runs |
| FORTRAN | `trebago` | numeric | key splitting maths | primary | runs |
| Ada | `eve` | limits | release conditions | primary | runs |
| APL | `kron` | arrays | keyholder matrix | primary | runs |
| MUMPS | `myra` | records | release policy tree | primary | runs |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id collet
matrix 309
name Collet
needs peers
needs journal
needs notify
needs fs.read
needs fs.write
needs sys.read

provides composite_key num high low
provides share_x100 num part whole
provides count_ok truth count cap
provides product num a b
logic kron composite_key <<
0|((_high*1000)+_low)
.
logic kron share_x100 <<
0|(_part*10000)%_whole
.
logic eve count_ok <<
eve collet
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron product <<
0|_a*_b
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000
```

## 6. The entry specifications (what each entry computes)

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

### `share_x100` (num) — kron

**Args.** `part whole`

**Semantics.** A part's share of a whole in hundredths of a percent, EXACT rational (the K's division is exact: 10000/3, not 3333). The caller floors.

**The exact check.** `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000`

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

### `product` (num) — kron

**Args.** `a b`

**Semantics.** a * b exactly in minor units. Integer only.

**The exact check.** `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `policies` tree in MUMPS as MYRA

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

