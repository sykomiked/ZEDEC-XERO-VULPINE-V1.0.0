# solarium — the exhaustive specification

| field | value |
|---|---|
| app id | `solarium` |
| matrix layer | 292 |
| icon group | L3 |
| wave spec | `wave24/292-solarium.md` |
| status | pending |

## 1. Purpose

looks after a garden or greenhouse: planting plans, watering, soil and light sensors, and harvest logs (`files.read`, `files.write`, `place.here`). zxvd has no method for soil and light sensor readings, valve control: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs location`
- `needs net`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use dibber 1` — call `<id>.<entry>` after the use line
- `use fen 1` — call `<id>.<entry>` after the use line
- `use isobar 1` — call `<id>.<entry>` after the use line
- `use homestead 1` — call `<id>.<entry>` after the use line
- `use weatherglass 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | seed and produce accounts | primary | runs |
| FORTRAN | `trebago` | numeric | evapotranspiration model | primary | runs |
| Ada | `eve` | limits | watering limits | primary | runs |
| APL | `kron` | arrays | bed sensor grid | primary | runs |
| MUMPS | `myra` | records | plant lineage | primary | runs |
| IEC 61131-3 Structured Text | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Modula-2 | `-` | structure | structure | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id solarium
matrix 292
name Solarium
needs fs.read
needs fs.write
needs location
needs net
needs notify

provides ratio_x1000 num num den
provides modular_draw num seed members step
provides count_ok truth count cap
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic kron modular_draw <<
0|(((_seed*1103515245)+12345)+(_step*7))|_members
.
logic eve count_ok <<
eve solarium
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
```

## 6. The entry specifications (what each entry computes)

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

### `modular_draw` (num) — kron

**Args.** `seed members step`

**Semantics.** The sortition's modular walk. Integer only, no hashing in the block. The residue operator is '|' in K ('%' is division).

**The exact check.** `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6`

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `water_ok` (Dibber's "may water now" AND soil below the declared moisture AND no rain in the forecast window) in Ada as Eve + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `beds` sensor grid in APL as KRON

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — `lineage` (plant lineage) in MUMPS as MYRA

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

