# lockbox — the exhaustive specification

| field | value |
|---|---|
| app id | `lockbox` |
| matrix layer | 9 |
| icon group | L1 |
| wave spec | `wave2/009-lockbox.md` |
| status | pending |

## 1. Purpose

ML-DSA identity keys, sealed hardware keys, saved passwords (`pqsec`, `identity`).

## 2. Capabilities (the exact registry names and scopes)

- `needs ledger.read`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use coffer 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| C++ | `cpp` | numeric | exact numerics | primary | runs (compiled to wasm) |
| COBOL | `sutra` | money | money and ledgers | primary | runs |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id lockbox
matrix 9
name Lockbox
needs ledger.read
needs notify

provides difference num a b
provides composite_key num high low
provides limit_excess num value limit
provides count_ok truth count cap
logic kron difference <<
0|_a-_b
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic eve count_ok <<
eve lockbox
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## 6. The entry specifications (what each entry computes)

### `difference` (num) — kron

**Args.** `a b`

**Semantics.** a - b exactly, never negative in the app's domain.

**The exact check.** `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — password strength score (length, classes, repeats as integer inputs) in sutra + kron witnessed

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

