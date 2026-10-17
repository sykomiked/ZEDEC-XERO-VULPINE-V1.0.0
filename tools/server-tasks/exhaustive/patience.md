# patience — the exhaustive specification

| field | value |
|---|---|
| app id | `patience` |
| matrix layer | 105 |
| icon group | L1 |
| wave spec | `wave9/105-patience.md` |
| status | pending |

## 1. Purpose

solitaire card games; saved games in the Banker (`files.read`, `files.write`).

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- none beyond the core (layer 0)

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Pascal | `pascal` | stepwise | step-by-step teaching | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id patience
matrix 105
name Patience
needs fs.read
needs fs.write

provides running_state truth state event
provides modular_draw num seed members step
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
logic kron modular_draw <<
0|(((_seed*1103515245)+12345)+(_step*7))|_members
.
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
```

## 6. The entry specifications (what each entry computes)

### `running_state` (truth) — kron

**Args.** `state event`

**Semantics.** The transition's legality: the (state,event) pairs that are allowed. A truth entry: both/neither are reachable.

**The exact check.** `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false`

### `modular_draw` (num) — kron

**Args.** `seed members step`

**Semantics.** The sortition's modular walk. Integer only, no hashing in the block. The residue operator is '|' in K ('%' is division).

**The exact check.** `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — games in myra

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

