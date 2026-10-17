# typecase — the exhaustive specification

| field | value |
|---|---|
| app id | `typecase` |
| matrix layer | 113 |
| icon group | L1 |
| wave spec | `wave2/113-typecase.md` |
| status | pending |

## 1. Purpose

preview, install and check fonts kept in the Hoard (`files.list`, `files.read`, `files.write`), and which scripts each covers. zxvd has no font-registry method: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use parley 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Groovy | `-` | scripting | scripting | primary | planned: needs Lingua front-end |
| Racket | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id typecase
matrix 113
name Typecase
needs fs.read
needs fs.write

provides count_ok truth count cap
provides share_x100 num part whole
logic eve count_ok <<
eve typecase
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron share_x100 <<
0|(_part*10000)%_whole
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
```

## 6. The entry specifications (what each entry computes)

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

### `share_x100` (num) — kron

**Args.** `part whole`

**Semantics.** A part's share of a whole in hundredths of a percent, EXACT rational (the K's division is exact: 10000/3, not 3333). The caller floors.

**The exact check.** `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000`

## 7. App-specific entries

None — this app is fully covered by the verified library. Emit the manifest in section 5 exactly.

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

