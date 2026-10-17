# talewind — the exhaustive specification

| field | value |
|---|---|
| app id | `talewind` |
| matrix layer | 108 |
| icon group | L1 |
| wave spec | `wave9/108-talewind.md` |
| status | pending |

## 1. Purpose

stories in the Banker (`files.read`, `files.write`); AI-drafted passages from `swarm.ask`.

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs ai.ask`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- none beyond the core (layer 0)

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Pascal | `pascal` | stepwise | step-by-step teaching | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id talewind
matrix 108
name Talewind
needs fs.read
needs fs.write
needs ai.ask

provides flags_none truth bits forbidden
logic kron flags_none <<
(_bits&_forbidden)=0
.
check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false
```

## 6. The entry specifications (what each entry computes)

### `flags_none` (truth) — kron

**Args.** `bits forbidden`

**Semantics.** No forbidden bit set: (bits & forbidden) == 0. A truth entry.

**The exact check.** `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — AI-drafted passages are held for the author's approval

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

