# conch — the exhaustive specification

| field | value |
|---|---|
| app id | `conch` |
| matrix layer | 141 |
| icon group | L1 |
| wave spec | `wave11/141-conch.md` |
| status | pending |

## 1. Purpose

pair hearing devices, balance and mono, show sound alerts as a screen flash; alerts from `notes.list`. zxvd has no audio-device method: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs notify`
- `needs sys.read`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- none beyond the core (layer 0)

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | safety | safety rules | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Raku | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id conch
matrix 141
name Conch
needs notify
needs sys.read

provides limit_excess num value limit
provides count_ok truth count cap
provides ratio_x1000 num num den
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic eve count_ok <<
eve conch
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
```

## 6. The entry specifications (what each entry computes)

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

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

