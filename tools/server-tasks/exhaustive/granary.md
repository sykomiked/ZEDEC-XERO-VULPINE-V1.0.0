# granary — the exhaustive specification

| field | value |
|---|---|
| app id | `granary` |
| matrix layer | 64 |
| icon group | L1 |
| wave spec | `wave7/064-granary.md` |
| status | pending |

## 1. Purpose

the model slot (`model.info`), room on disk (`sys.disk`), energy (`power.status`). Downloading a model and reading its licence have no method: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs net`
- `needs sys.read`
- `needs fs.read`
- `needs fs.write`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use caravan 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id granary
matrix 64
name Granary
needs net
needs sys.read
needs fs.read
needs fs.write

provides flags_none truth bits forbidden
provides composite_key num high low
logic kron flags_none <<
(_bits&_forbidden)=0
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
```

## 6. The entry specifications (what each entry computes)

### `flags_none` (truth) — kron

**Args.** `bits forbidden`

**Semantics.** No forbidden bit set: (bits & forbidden) == 0. A truth entry.

**The exact check.** `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — licence allows this use (licence id in the allowed list, personal or commercial flag) in eve + kron witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — "fits on disk" in sutra + kron witnessed

**Runtime.** `kron` — the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic kron <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — A model whose licence does not allow the use is never loaded

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic eve <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.4 — the owner decides

**Runtime.** `-` — 

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic - <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

