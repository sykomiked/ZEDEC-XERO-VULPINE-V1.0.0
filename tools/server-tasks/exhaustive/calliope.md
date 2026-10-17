# calliope — the exhaustive specification

| field | value |
|---|---|
| app id | `calliope` |
| matrix layer | 374 |
| icon group | L3 |
| wave spec | `wave31/374-calliope.md` |
| status | pending |

## 1. Purpose

Children create illustrated stories and games with a gentle AI helper in a safe, parent-guided space: stories in the Hoard (`files.read`, `files.write`), helper suggestions from `swarm.ask` (held for the child and parent), time from `screen.check` and `screen.status`, the family's seats (`seats.list`).

## 2. Capabilities (the exact registry names and scopes)

- `needs fs.read`
- `needs fs.write`
- `needs ai.ask`
- `needs screen.time`
- `needs sys.read`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use spindrift 1` — call `<id>.<entry>` after the use line
- `use rill 1` — call `<id>.<entry>` after the use line
- `use limner 1` — call `<id>.<entry>` after the use line
- `use glen 1` — call `<id>.<entry>` after the use line
- `use cubby 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | allowance credits | primary | runs |
| FORTRAN | `trebago` | numeric | animation timing | primary | runs |
| Ada | `eve` | limits | screen-time limits | primary | runs |
| APL | `kron` | arrays | story grid | primary | runs |
| MUMPS | `myra` | records | family tree | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Pascal | `pascal` | stepwise | step-by-step teaching | primary | runs |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id calliope
matrix 374
name Calliope
needs fs.read
needs fs.write
needs ai.ask
needs screen.time
needs sys.read

provides limit_excess num value limit
provides ratio_x1000 num num den
provides running_state truth state event
logic trebago limit_excess <<
trebago limit_excess
  in  value, limit
  out excess
  excess = max(0, value - limit)
end
.
logic kron ratio_x1000 <<
0|(_num*1000)%_den
.
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## 6. The entry specifications (what each entry computes)

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `ratio_x1000` (num) — kron

**Args.** `num den`

**Semantics.** The ratio in thousandths, exact rational. The caller floors.

**The exact check.** `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 1000/3,125,1,750`

### `running_state` (truth) — kron

**Args.** `state event`

**Semantics.** The transition's legality: the (state,event) pairs that are allowed. A truth entry: both/neither are reachable.

**The exact check.** `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — - `steps`: story-making steps one at a time in BASIC (twin Hatchling)

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — - `story`: story grid in APL as KRON

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — - `allowance`: allowance credits (credits for finished stories, refusing on overflow) in COBOL as Sutra

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.4 — - `family`: family tree in MUMPS as MYRA

**Runtime.** `eve` — the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic eve <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.5 — - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)

**Runtime.** `kron` — the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic kron <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.6 — Paraconsistent (BOTH test, in test_zxapp.c): the child and the helper want different endings: the ending is BOTH

**Runtime.** `myra` — the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic myra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.7 — never re-derive the tables

**Runtime.** `python` — the host tier (via the SDK): tooling and glue, exact integers.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic python <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

## 8. The dual interface (human and AI)

- **Human.** The app's icon (its matrix slot's ZXVG) launches it from the desk; its entries' views render on the framebuffer.
- **AI.** Every provided entry is callable through the app runtime (`zxapp_call`) and the `--ai-manifest` surface; the same state the human sees.

## 9. The test plan

- The node loads the manifest (`zxapp_load`): every logic entry compiles.
- Every check line runs and passes (`zxapp_check`): the exact answers.
- The browser engine (`zxv_app.wasm`) loads the same manifest and passes the same checks.

