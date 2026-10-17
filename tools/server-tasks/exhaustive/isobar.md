# isobar — the exhaustive specification

| field | value |
|---|---|
| app id | `isobar` |
| matrix layer | 239 |
| icon group | L2 |
| wave spec | `wave19/239-isobar.md` |
| status | pending |

## 1. Purpose

Runs a local weather station network: stations and sites (`place.here`, `place.list`), station nodes (`devmesh.devices`, `net.peers`), records in the Hoard (`files.read`, `files.write`). Sensor input has no method: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs location`
- `needs peers`
- `needs net`
- `needs fs.read`
- `needs fs.write`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use weatherglass 1` — call `<id>.<entry>` after the use line
- `use homestead 1` — call `<id>.<entry>` after the use line
- `use furrow 1` — call `<id>.<entry>` after the use line
- `use trellis 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | observation records | primary | runs |
| FORTRAN | `trebago` | numeric | forecast model | primary | runs |
| Ada | `eve` | limits | sensor range limits | primary | runs |
| APL | `kron` | arrays | station grid | primary | runs |
| MUMPS | `myra` | records | site tree | primary | runs |
| IEC 61131-3 Structured Text | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Elixir | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Clojure | `-` | dataflow | dataflow | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id isobar
matrix 239
name Isobar
needs location
needs peers
needs net
needs fs.read
needs fs.write
needs notify

provides limit_excess num value limit
provides composite_key num high low
provides difference num a b
provides count_ok truth count cap
provides within_range truth value lo hi
provides running_state truth state event
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
logic kron difference <<
0|_a-_b
.
logic eve count_ok <<
eve isobar
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
logic eve within_range <<
eve isobar
values = 0 .. 100

within_range {
  value: values 0 .. 100
  lo: values 0 .. 0
  hi: values 100 .. 100
}
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## 6. The entry specifications (what each entry computes)

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

### `difference` (num) — kron

**Args.** `a b`

**Semantics.** a - b exactly, never negative in the app's domain.

**The exact check.** `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1`

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

### `within_range` (truth) — eve

**Args.** `value lo hi`

**Semantics.** The range law. true when lo <= value <= hi, false outside. Both bounds inclusive. A truth entry: the four values apply (a contradicted claim is both, no evidence is neither).

**The exact check.** `check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false`

### `running_state` (truth) — kron

**Args.** `state event`

**Semantics.** The transition's legality: the (state,event) pairs that are allowed. A truth entry: both/neither are reachable.

**The exact check.** `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — - (critical) short forecast from the 3-hour pressure tendency (tenths of a hPa) by rule table in FORTRAN as Trebago + APL as KRON witnessed

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — - station grid (stations x hours) in APL as KRON

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — never re-derive the tables

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

