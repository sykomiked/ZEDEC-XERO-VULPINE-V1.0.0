# rigging — the complete brief

| | |
|---|---|
| matrix layer | 381 |
| icon group | L3 |
| wave spec | `wave32/381-rigging.md` |

## Purpose

Orchestrates many services, agents and machines from one console, with budgets and safe rollbacks: agents (`swarm.state`, `swarm.ask`), engines (`features.get`, `features.set` owner-approved), machines (`net.peers`, `devmesh.devices`, `sys.info`), budgets (`power.budget`, `power.top`), versions to roll back to (`backup.versions`, `backup.restore` as a new item).

## Capabilities (the exact registry names for `needs`)

- `swarm`
- `ai.ask`
- `peers`
- `net`
- `sys.read`
- `backup`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | compute billing | primary | runs |
| FORTRAN | `trebago` | numeric | load forecasting | primary | runs |
| Ada | `eve` | limits | budget and rate limits | primary | runs |
| APL | `kron` | arrays | job arrays | primary | runs |
| MUMPS | `myra` | records | service tree | primary | runs |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |
| Elixir | `-` | faults | fault handling | primary | planned: needs Lingua front-end |
| JCL | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Occam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `deploy_ok`: (critical) budget and rate limits (each job within its budget AND at most the rate per minute) in Ada as Eve + APL as KRON witnessed
- - `forecast`: (critical) load forecast (next hour's load from the last n hours, integer least squares) in FORTRAN as Trebago + APL as KRON witnessed
- - `rollback`: rollback plan (the last version every dependant agrees on) in APL as KRON
- - `jobs`: job arrays (jobs x machines) in APL as KRON
- - `billing`: compute billing (minor units, refusing on overflow) in COBOL as Sutra
- - `services`: service tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two agents report a branch as running and stopped: the branch is BOTH
- the test asserts state 3 and that only that branch pauses while the rest keep running. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id rigging
matrix 381
name Rigging
needs swarm
needs ai.ask
needs peers
needs net
needs sys.read
needs backup
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

