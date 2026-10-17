# dyer — the exhaustive specification

| field | value |
|---|---|
| app id | `dyer` |
| matrix layer | 116 |
| icon group | L1 |
| wave spec | `wave10/116-dyer.md` |
| status | pending |

## 1. Purpose

pick colours, convert between RGB, HSL and hex, check contrast, keep palettes in the Hoard (`files.read`, `files.write`). Picking from the screen has no zxvd method: use only existing methods; list what's missing in your report.

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
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| JOSS | `-` | formula | formulas | primary | planned: needs Lingua front-end |
| Perl | `-` | text | text | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id dyer
matrix 116
name Dyer
needs fs.read
needs fs.write

provides ratio_x1000 num num den
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
```

## 6. The entry specifications (what each entry computes)

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

## 7. App-specific entries

None — this app is fully covered by the verified library. Emit the manifest in section 5 exactly.

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

