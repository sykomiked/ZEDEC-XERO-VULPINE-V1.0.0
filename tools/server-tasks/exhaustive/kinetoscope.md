# kinetoscope — the exhaustive specification

| field | value |
|---|---|
| app id | `kinetoscope` |
| matrix layer | 51 |
| icon group | L1 |
| wave spec | `wave4/051-kinetoscope.md` |
| status | pending |

## 1. Purpose

videos from the Hoard (`files.read`), cut for streaming (`stream.plan`, `stream.segment`), watched together (`hall.load`, `hall.play`, `hall.pause`, `hall.seek`, `hall.status`). Local video output has no method: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs peers`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use witness 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |
| MATLAB | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id kinetoscope
matrix 51
name Kinetoscope
needs fs.read
needs peers

provides ratio_x1000 num num den
provides limit_excess num value limit
provides exact_total num n a b c d
provides composite_key num high low
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic trebago exact_total <<
trebago exact_total
  in  n, a, b, c, d
  out total
  total = sum(i = 1:n, (i=1)*a + (i=2)*b + (i=3)*c + (i=4)*d)
end
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## 6. The entry specifications (what each entry computes)

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `exact_total` (num) — trebago

**Args.** `n a b c d`

**Semantics.** The exact sum of the first n of the four sizes. Integer only, no rounding, no overflow silently: the caller checks the bound.

**The exact check.** `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — fit to the window (letterbox from width and height) in kron + C++ wasm witnessed

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

