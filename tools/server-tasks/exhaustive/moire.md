# moire — the exhaustive specification

| field | value |
|---|---|
| app id | `moire` |
| matrix layer | 380 |
| icon group | L3 |
| wave spec | `wave32/380-moire.md` |
| status | pending |

## 1. Purpose

Adapts any app's look, language and input to each person: simple mode (`ui.get`, `ui.set`), languages (`xlate.languages`, `xlate.detect`, `xlate.translate`), preferences in the Hoard (`files.read`, `files.write`). Display filters and input methods have no method (as Tint and Glyphwright): use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs sys.read`
- `needs fs.read`
- `needs fs.write`
- `needs ai.ask`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use shallows 1` — call `<id>.<entry>` after the use line
- `use lampwright 1` — call `<id>.<entry>` after the use line
- `use hornbook 1` — call `<id>.<entry>` after the use line
- `use glyphwright 1` — call `<id>.<entry>` after the use line
- `use tint 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | licence records | primary | runs |
| FORTRAN | `trebago` | numeric | contrast maths | primary | runs |
| Ada | `eve` | limits | readability minimums | primary | runs |
| APL | `kron` | arrays | layout arrays | primary | runs |
| MUMPS | `myra` | records | preference tree | primary | runs |
| Raku | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Ruby | `ruby` | scripting | scripting | primary | planned: SDK host only, no in-app runtime yet |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id moire
matrix 380
name Moire
needs sys.read
needs fs.read
needs fs.write
needs ai.ask

provides within_range truth value lo hi
provides ratio_x1000 num num den
provides composite_key num high low
provides clamp num value lo hi
provides running_state truth state event
logic eve within_range <<
eve moire
values = 0 .. 100

within_range {
  value: values 0 .. 100
  lo: values 0 .. 0
  hi: values 100 .. 100
}
.
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic kron composite_key <<
0|((_high*1000)+_low)
.
logic kron clamp <<
0|max(_lo, min(_hi, _value))
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false
check within_range value=0,100 lo=0,0 hi=100,100 = true,true
check within_range value=50,101 lo=0,0 hi=100,100 = true,false
check within_range value=1,99 lo=0,0 hi=100,100 = true,true
check within_range value=100 lo=0 hi=100 = true
check within_range value=0 lo=0 hi=100 = true
check within_range value=101 lo=0 hi=100 = false
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## 6. The entry specifications (what each entry computes)

### `within_range` (truth) — eve

**Args.** `value lo hi`

**Semantics.** The range law. true when lo <= value <= hi, false outside. Both bounds inclusive. A truth entry: the four values apply (a contradicted claim is both, no evidence is neither).

**The exact check.** `['check within_range value=0,50,100,101 lo=0,0,0,0 hi=100,100,100,100 = true,true,true,false', 'check within_range value=0,100 lo=0,0 hi=100,100 = true,true', 'check within_range value=50,101 lo=0,0 hi=100,100 = true,false', 'check within_range value=1,99 lo=0,0 hi=100,100 = true,true', 'check within_range value=100 lo=0 hi=100 = true', 'check within_range value=0 lo=0 hi=100 = true', 'check within_range value=101 lo=0 hi=100 = false']`

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

### `clamp` (num) — kron

**Args.** `value lo hi`

**Semantics.** The value clamped into [lo,hi]. The result is always within the bounds.

**The exact check.** `check clamp value=-5,0,50,150 lo=0,0,0,0 hi=100,100,100,100 = 0,0,50,100`

### `running_state` (truth) — kron

**Args.** `state event`

**Semantics.** The transition's legality: the (state,event) pairs that are allowed. A truth entry: both/neither are reachable.

**The exact check.** `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — - `layout`: layout arrays (elements x size and order for the person's profile) in APL as KRON

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — - `prefs`: preference tree in MUMPS as MYRA

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`used.sutra`):**

```
record used-share
  total: 9(18)
  left: 9(18)
  used: 9(5)
procedure
  compute used = (total - left) * 10000 / total
  answer used
```

### 7.3 — Paraconsistent (BOTH test, in test_zxapp.c): high contrast and colour coding conflict for one chart: the display is BOTH

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`count.tb`):**

```
trebago kind_count
  in  kind(n), want
  out count
  count = sum(i, max(0, min(1, 1 + kind(i) - want)) * max(0, min(1, 1 + want - kind(i))))
end
```

### 7.4 — never re-derive the tables

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic eve <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

**A real block in this runtime — copy this spelling (`transfer.eve`):**

```
eve coffer
minor_amount = 1 .. 9007199254740991
minor_left = 0 .. 999999999999999999
minor_gap = 0 .. 0

transfer_rule {
  amount: minor_amount
  after: minor_left
  room: minor_left
  gap: minor_gap
}
```

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

