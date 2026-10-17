# glimpse — the exhaustive specification

| field | value |
|---|---|
| app id | `glimpse` |
| matrix layer | 111 |
| icon group | L1 |
| wave spec | `wave9/111-glimpse.md` |
| status | pending |

## 1. Purpose

capture the screen, a window or a region, record the screen; save into the Hoard with `files.write` ("Screenshot N.png", first free N from `files.list`); cut recordings with `stream.plan`. zxvd has no display-capture method: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs sys.read`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- none beyond the core (layer 0)

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Visual Basic | `dotnet` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |
| dBase | `-` | records | records | primary | planned: needs Lingua front-end |
| ALGOL 68 | `-` | formula | formulas | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id glimpse
matrix 111
name Glimpse
needs fs.read
needs fs.write
needs sys.read
needs notify

provides limit_excess num value limit
provides clamp num value lo hi
provides count_ok truth count cap
provides composite_key num high low
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron clamp <<
0|max(_lo, min(_hi, _value))
.
logic eve count_ok <<
eve glimpse
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## 6. The entry specifications (what each entry computes)

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `clamp` (num) — kron

**Args.** `value lo hi`

**Semantics.** The value clamped into [lo,hi]. The result is always within the bounds.

**The exact check.** `check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100`

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — first free N from the used numbers vector in kron

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

