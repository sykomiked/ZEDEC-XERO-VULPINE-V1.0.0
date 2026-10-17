# keystone — the exhaustive specification

| field | value |
|---|---|
| app id | `keystone` |
| matrix layer | 4 |
| icon group | L1 |
| wave spec | `wave3/004-keystone.md` |
| status | pending |

## 1. Purpose

displays, sound, keyboard, language, accessibility, network, sharing, the Hoard, model choice, budgets (docs/SYSTEM_UTILITIES.md).

## 2. Capabilities (the exact registry names and scopes)

- `needs sys.read`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use hollow 1` — call `<id>.<entry>` after the use line
- `use talisman 1` — call `<id>.<entry>` after the use line
- `use crown 1` — call `<id>.<entry>` after the use line
- `use satchel 1` — call `<id>.<entry>` after the use line
- `use typecase 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| COBOL | `sutra` | money | money and ledgers | primary | runs |
| C++ | `cpp` | money | money and ledgers | witness | runs (compiled to wasm) |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id keystone
matrix 4
name Keystone
needs sys.read
needs notify

provides limit_excess num value limit
provides within_range truth value lo hi
provides share_x100 num part whole
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic eve within_range <<
eve keystone
values = 0 .. 100

within_range {
  value: values 0 .. 100
  lo: values 0 .. 0
  hi: values 100 .. 100
}
.
logic kron share_x100 <<
0|(_part*10000)%_whole
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false
check share_x100 part=1,1,1,50 whole=3,4,100,100 = 10000/3,2500,100,5000
```

## 6. The entry specifications (what each entry computes)

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `within_range` (truth) — eve

**Args.** `value lo hi`

**Semantics.** The range law. true when lo <= value <= hi, false outside. Both bounds inclusive. A truth entry: the four values apply (a contradicted claim is both, no evidence is neither).

**The exact check.** `check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false`

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

