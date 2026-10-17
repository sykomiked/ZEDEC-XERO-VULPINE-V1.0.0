# windbreak — the exhaustive specification

| field | value |
|---|---|
| app id | `windbreak` |
| matrix layer | 222 |
| icon group | L2 |
| wave spec | `wave18/222-windbreak.md` |
| status | pending |

## 1. Purpose

Fronts the Pit Boss and the Cage (docs/PIT_BOSS.md, docs/CAGE.md; admission in kernel/src/immigration, capabilities in kernel/src/capctl, slot, layer, budget and needs in kernel/src/apps/zxapp.c via `zxapp_slot_layer` and `zxapp_needs`, scheduling in kernel/src/sched). Shows consumers and their energy (`power.top`, `power.status`), the machine (`sys.info`), engines and switches (`features.get`), agents and budgets (`swarm.state`), events and rule actions (`log.tail`, `journal.query`). A running-program table, the Cage's cleared/held/refused queue and the rulebook have no method: use only existing methods; list what's missing in your report.

## 2. Capabilities (the exact registry names and scopes)

- `needs sys.read`
- `needs journal`
- `needs swarm`
- `needs notify`

These are the ONLY capability names the loader accepts. A name not in the kernel's table refuses the whole manifest.

## 3. Dependencies (the built apps this one calls)

- `use tidewatch 1` — call `<id>.<entry>` after the use line
- `use warren 1` — call `<id>.<entry>` after the use line
- `use gatehouse 1` — call `<id>.<entry>` after the use line
- `use wake 1` — call `<id>.<entry>` after the use line
- `use twelvefold 1` — call `<id>.<entry>` after the use line

## 4. The language bindings (which language owns which component)

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | rule-firing log | primary | runs |
| FORTRAN | `trebago` | numeric | tick and memory budgets | primary | runs |
| Ada | `eve` | limits | crossing rules between L1, L2 and L3 | primary | runs |
| APL | `kron` | arrays | process table | primary | runs |
| MUMPS | `myra` | records | quarantine records | primary | runs |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |
| Perl | `-` | text | text | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

## 5. The complete manifest — VERIFIED (the library's proven bodies)

Every line is final: these bodies and checks were proven against a running node (satchel: loaded, checks passed). Emit exactly.

```zxapp
zxapp 1
id windbreak
matrix 222
name Windbreak
needs sys.read
needs journal
needs swarm
needs notify

provides count_ok truth count cap
provides limit_excess num value limit
provides composite_key num high low
provides running_state truth state event
logic eve count_ok <<
eve windbreak
counts = 1 .. 100

count_ok {
  count: counts 1 .. 100
  cap: counts 100 .. 100
}
.
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
logic kron running_state <<
((_state=0)&(_event=1))|((_state=1)&(_event=1))
.
check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false
check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000
check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999
check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false
```

## 6. The entry specifications (what each entry computes)

### `count_ok` (truth) — eve

**Args.** `count cap`

**Semantics.** The registry law. true when 0 < count <= cap. A truth entry: the four values apply.

**The exact check.** `check count_ok count=1,100,101,0 cap=100,100,100,100 = true,true,false,false`

### `limit_excess` (num) — trebago

**Args.** `value limit`

**Semantics.** The excess over a ceiling. 0 when value <= limit, else value - limit. Integer only; the limit is inclusive; no negative excess ever.

**The exact check.** `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000`

### `composite_key` (num) — kron

**Args.** `high low`

**Semantics.** The record's composite index: high*1000 + low. Explicit parens: K evaluates right to left.

**The exact check.** `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999`

### `running_state` (truth) — kron

**Args.** `state event`

**Semantics.** The transition's legality: the (state,event) pairs that are allowed. A truth entry: both/neither are reachable.

**The exact check.** `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false`

## 7. The app-specific entries (spelled out to the letter)

The library does not cover these clauses. Each below gives the clause, the runtime, and the required shapes.

### 7.1 — - an item's clearance joins its checks with `lpres_conjoin` in C (kernel order). docs/CAGE.md says "Belnap conjunction"

**Runtime.** `c` — the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic c <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.2 — CLAUDE.md says kernel order, not Belnap: use `lpres_conjoin` and report the conflict

**Runtime.** `sutra` — the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic sutra <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.3 — Paraconsistent (BOTH test, in test_zxapp.c): check A clears an item (TRUE) and check B refuses it (FALSE), giving BOTH

**Runtime.** `trebago` — the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).

**provides line:** `provides <entry> num|truth <arg> <arg> ...`

**body:** `logic trebago <entry> <<` then the source then `.`

**check:** `check <entry> <arg>=<int>,... = <answer>,...`

### 7.4 — never re-derive the tables

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

