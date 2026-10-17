# conclave — the complete brief

| | |
|---|---|
| matrix layer | 369 |
| icon group | L3 |
| wave spec | `wave31/369-conclave.md` |

## Purpose

Runs your local AI swarm: choose models for this machine (`model.info`, `power.status`, `power.top`), set token budgets (`power.budget`, `swarm.state`), see each agent's work (`swarm.state`, `journal.query`) and accept or refuse its suggestions (`swarm.ask` answers held, `play.draft` untrusted). Changing the model slot and per-agent token budgets have no method beyond `power.budget`: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `ai.ask`
- `swarm`
- `sys.read`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | cycle ledger | primary | runs |
| FORTRAN | `trebago` | numeric | budget arithmetic | primary | runs |
| Ada | `eve` | limits | budget limits | primary | runs |
| APL | `kron` | arrays | agent tensor tables | primary | runs |
| MUMPS | `myra` | records | agent records | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **-** (Occam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `accept_ok`: (critical) "act on this suggestion" (the person accepted it AND Sieve's `may this be applied` TRUE AND the agents agree or the person chose a side) in Ada as Eve + APL as KRON witnessed
- nothing acts on BOTH
- - `budget`: (critical) token budget per agent per cycle over the D1-D9 faces (sums exact, refusing on overflow) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - `budget_ok`: budget limits (no agent above its cap, the swarm total within the owner's) in Ada as Eve
- - `agents`: agent tensor tables (agents x tasks x tokens) in APL as KRON
- - `cycles`: cycle ledger (cycles run and tokens spent, refusing on overflow) in COBOL as Sutra
- - `records`: agent records in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two agents give different answers with evidence: the result is BOTH
- the test asserts state 3 and that both answers and their evidence are shown and nothing is acted on until the person accepts one. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id conclave
matrix 369
name Conclave
needs ai.ask
needs swarm
needs sys.read
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

