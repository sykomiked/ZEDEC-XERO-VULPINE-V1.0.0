# windbreak — the complete brief

| | |
|---|---|
| matrix layer | 222 |
| icon group | L2 |
| wave spec | `wave18/222-windbreak.md` |

## Purpose

Fronts the Pit Boss and the Cage (docs/PIT_BOSS.md, docs/CAGE.md; admission in kernel/src/immigration, capabilities in kernel/src/capctl, slot, layer, budget and needs in kernel/src/apps/zxapp.c via `zxapp_slot_layer` and `zxapp_needs`, scheduling in kernel/src/sched). Shows consumers and their energy (`power.top`, `power.status`), the machine (`sys.info`), engines and switches (`features.get`), agents and budgets (`swarm.state`), events and rule actions (`log.tail`, `journal.query`). A running-program table, the Cage's cleared/held/refused queue and the rulebook have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `journal`
- `swarm`
- `notify`

## The language map: which language owns which component

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

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "this crossing is allowed" by docs/REGISTRY.md section 4 (L3 to L2 as a result for checking, L2 to L1 only as a checked input event, L1 only its own journal) in Ada as Eve + APL as KRON witnessed
- - (critical) "within budget" (ticks and bytes used against the manifest's budget) in FORTRAN as Trebago + APL as KRON witnessed
- - process table (program x layer, budget, capabilities) in APL as KRON
- - rule-firing counts per rule as fixed records refusing on overflow in COBOL as Sutra
- - quarantine records (item, desk, verdict, evidence) in MUMPS as MYRA
- - an item's clearance joins its checks with `lpres_conjoin` in C (kernel order). docs/CAGE.md says "Belnap conjunction"
- CLAUDE.md says kernel order, not Belnap: use `lpres_conjoin` and report the conflict
- Paraconsistent (BOTH test, in test_zxapp.c): check A clears an item (TRUE) and check B refuses it (FALSE), giving BOTH
- the test asserts state 3, the item stays held in the Cage, and admission is false (never admitted by default). Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id windbreak
matrix 222
name Windbreak
needs sys.read
needs journal
needs swarm
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

