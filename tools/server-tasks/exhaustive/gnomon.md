# gnomon — the exhaustive specification

| field | value |
|---|---|
| app id | `gnomon` |
| matrix layer | 107 |
| icon group | L1 |
| wave spec | `wave9/107-gnomon.md` |
| status | pending |

## 1. Purpose

exact shapes and numbers; saves in the Banker (`files.read`, `files.write`).

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use number_classics 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| F# | `dotnet` | numeric | exact numerics | primary | planned: SDK host only, no in-app runtime yet |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Logo | `logo` | geometry | geometry | primary | runs |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id gnomon
matrix 107
name Gnomon
needs fs.read
needs fs.write

provides share_x100 num part whole
logic kron share_x100 <<
0|(_part*10000)%_whole
.
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
```

## 6. The entry specifications (what each entry computes)

### `share_x100` (num) — kron

**Args.** `part whole`

**Semantics.** A part's share of a whole in hundredths of a percent, EXACT rational (the K's division is exact: 10000/3, not 3333). The caller floors.

**The exact check.** `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — twice a polygon's area (shoelace, exact) and squared side lengths in kron + C++ wasm witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — saved figures in myra. Turtle paths stay in C (Logo has no runtime)

**Runtime.** `myra` — the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic myra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — say so in the manifest

**Runtime.** `kron` — the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic kron <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

