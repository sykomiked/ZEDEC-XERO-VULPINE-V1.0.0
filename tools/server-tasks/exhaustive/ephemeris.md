# ephemeris — the exhaustive specification

| field | value |
|---|---|
| app id | `ephemeris` |
| matrix layer | 299 |
| icon group | L3 |
| wave spec | `wave25/299-ephemeris.md` |
| status | pending |

## 1. Purpose

plans observing nights: object positions, rise and set times, weather and a log of what you saw (`place.here`, `sys.info`, `net.peers`, `files.write`). zxvd has no method for weather source: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs location`
- `needs sys.read`
- `needs camera`
- `needs fs.write`
- `needs peers`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use astrolabe 1` — call `<id>.<entry>` after the use line
- `use linkman 1` — call `<id>.<entry>` after the use line
- `use escapement 1` — call `<id>.<entry>` after the use line
- `use isobar 1` — call `<id>.<entry>` after the use line
- `use orrery 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | equipment ledger | primary | runs |
| FORTRAN | `trebago` | numeric | orbital mechanics | primary | runs |
| Ada | `eve` | limits | exposure limits | primary | runs |
| APL | `kron` | arrays | sky coordinate arrays | primary | runs |
| MUMPS | `myra` | records | observation catalogue | primary | runs |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |
| Oberon | `-` | structure | structure | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id ephemeris
matrix 299
name Ephemeris
needs location
needs sys.read
needs camera
needs fs.write
needs peers

provides limit_excess num value limit
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
```

## 6. The entry specifications (what each entry computes)

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `orbit` (rise, transit and set in minutes of day) in FORTRAN as Trebago + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `equipment` ledger (items in minor units) in COBOL as Sutra

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — `sky` coordinate arrays in APL as KRON

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.4 — `observations` catalogue in MUMPS as MYRA

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

