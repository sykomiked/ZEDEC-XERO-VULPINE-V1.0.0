# massif — the complete brief

| | |
|---|---|
| matrix layer | 256 |
| icon group | L2 |
| wave spec | `wave21/256-massif.md` |

## Purpose

Spreads large integer compute jobs across machines and tracks results: functions and data (`aipi.put`, `aipi.get`, `aipi.publish`, `aipi.execute`, `aipi.status`), other nodes (`peer.ask`, `peer.status`, `net.peers`), energy and budgets (`power.budget`, `power.status`, `swarm.state`).

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `swarm`
- `sys.read`
- `ledger.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | job accounting | primary | runs |
| FORTRAN | `trebago` | numeric | numerical kernels | primary | runs |
| Ada | `eve` | limits | resource limits | primary | runs |
| APL | `kron` | arrays | work-unit array | primary | runs |
| MUMPS | `myra` | records | job tree | primary | runs |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |
| Bash | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Occam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) work-unit plan (a range split into units, each assigned to at least two nodes) in APL as KRON + FORTRAN as Trebago witnessed
- - (critical) "may send this unit" (fuel, energy and price per unit within the owner's caps) in Ada as Eve + APL as KRON witnessed
- - (critical) job cost (price per unit plus the 0.08889% fee = 8889/10,000,000, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - the job's own integer kernels (sums, modular powers) in FORTRAN as Trebago
- - job tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two nodes return different results for the same unit, giving BOTH
- the test (loopback only) asserts state 3 and a re-run on a third node, the result not accepted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id massif
matrix 256
name Massif
needs peers
needs net
needs swarm
needs sys.read
needs ledger.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

