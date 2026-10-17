# lampwright — the exhaustive specification

| field | value |
|---|---|
| app id | `lampwright` |
| matrix layer | 205 |
| icon group | L2 |
| wave spec | `wave17/205-lampwright.md` |
| status | pending |

## 1. Purpose

sets ambient lighting and display scenes across rooms and screens with colour care, reading `ui.get`, `sys.info` and `power.status`. zxvd has no method for lighting device control, display colour set: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs sys.read`
- `needs net`
- `needs peers`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use relief 1` — call `<id>.<entry>` after the use line
- `use homestead 1` — call `<id>.<entry>` after the use line
- `use dyer 1` — call `<id>.<entry>` after the use line
- `use tint 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | energy use | primary | runs |
| FORTRAN | `trebago` | numeric | colour math | primary | runs |
| Ada | `eve` | limits | brightness limits | primary | runs |
| APL | `kron` | arrays | light grid | primary | runs |
| MUMPS | `myra` | records | room tree | primary | runs |
| Dart | `dart` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |
| Ladder logic | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Smalltalk | `-` | objects | domain objects | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id lampwright
matrix 205
name Lampwright
needs sys.read
needs net
needs peers
needs notify

provides limit_excess num value limit
provides within_range truth value lo hi
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic eve within_range <<
eve lampwright
values = 0 .. 100

within_range {
  value: values 0 .. 100
  lo: values 0 .. 0
  hi: values 100 .. 100
}
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false
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

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — colour temperature to RGB (table by kelvin, interpolated) in APL as KRON + FORTRAN as Trebago witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — energy use (Wh x tariff in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — light grid in APL as KRON

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.4 — room tree in MUMPS as MYRA

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic eve <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

