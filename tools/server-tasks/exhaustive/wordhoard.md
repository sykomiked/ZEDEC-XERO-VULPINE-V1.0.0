# wordhoard — the exhaustive specification

| field | value |
|---|---|
| app id | `wordhoard` |
| matrix layer | 46 |
| icon group | L1 |
| wave spec | `wave6/046-wordhoard.md` |
| status | pending |

## 1. Purpose

word lists kept in the Hoard (`files.read`, `search.query`), the language of a word (`xlate.detect`, `xlate.languages`); a definition from the model (`swarm.ask`) is held until the owner accepts it.

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs search`
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
| AWK | `-` | text | text | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |
| PostScript | `-` | layout | page layout | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id wordhoard
matrix 46
name Wordhoard
needs fs.read
needs search
needs ai.ask

provides composite_key num high low
provides share_x100 num part whole
logic kron composite_key <<
0|((_high*1000)+_low)
.
logic kron share_x100 <<
0|(_part*10000)%_whole
.
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
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

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — "did you mean" edit distance over code points in C++ wasm

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

