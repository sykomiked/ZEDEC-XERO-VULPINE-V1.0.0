# luminaire — the exhaustive specification

| field | value |
|---|---|
| app id | `luminaire` |
| matrix layer | 308 |
| icon group | L3 |
| wave spec | `wave26/308-luminaire.md` |
| status | pending |

## 1. Purpose

controls lighting across a home or building, saving energy with schedules, presence and daylight (`power.status`, `power.budget`, `sys.info`, `files.read`, `files.write`). zxvd has no method for light and presence device control: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs sys.read`
- `needs fs.read`
- `needs fs.write`
- `needs notify`
- `needs location`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use levin 1` — call `<id>.<entry>` after the use line
- `use wisp 1` — call `<id>.<entry>` after the use line
- `use escapement 1` — call `<id>.<entry>` after the use line
- `use homestead 1` — call `<id>.<entry>` after the use line
- `use brazier 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | energy bills | primary | runs |
| FORTRAN | `trebago` | numeric | daylight model | primary | runs |
| Ada | `eve` | limits | circuit load limits | primary | runs |
| APL | `kron` | arrays | room light grid | primary | runs |
| MUMPS | `myra` | records | building zone tree | primary | runs |
| Bash | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| IEC 61131-3 Structured Text | `-` | control | machine control | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id luminaire
matrix 308
name Luminaire
needs sys.read
needs fs.read
needs fs.write
needs notify
needs location

provides count_ok truth count cap
provides grade_band num value b1 b2 b3 b4
logic eve count_ok <<
eve luminaire
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic kron grade_band <<
(_value>_b1)+(_value>_b2)+(_value>_b3)+(_value>_b4)
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4
```

## 6. The entry specifications (what each entry computes)

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

### `grade_band` (num) — kron

**Args.** `value b1 b2 b3 b4`

**Semantics.** The band index 0..4 of the value across the four ascending bounds. The comparisons sum (each true adds 1).

**The exact check.** `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — `energy_bills` (Wh x tariff in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — `rooms` light grid in APL as KRON

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — `zones` tree in MUMPS as MYRA

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

