# skep — the complete brief

| | |
|---|---|
| matrix layer | 185 |
| icon group | L2 |
| wave spec | `wave15/185-skep.md` |

## Purpose

coordinates a swarm of agents and nodes on shared work (`swarm.state`, `swarm.earnings`, `net.peers`, `peer.status`) with bounties and patronage grants from the books (`ledger.balance`).

## Capabilities (the exact registry names for `needs`)

- `swarm`
- `peers`
- `net`
- `ledger.read`
- `ledger.post`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | grant ledger | primary | runs |
| FORTRAN | `trebago` | numeric | load balancing | primary | runs |
| Ada | `eve` | limits | agent budgets | primary | runs |
| APL | `kron` | arrays | task matrix | primary | runs |
| MUMPS | `myra` | records | colony tree | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- grant ledger (each contributor's grant by weight, largest remainder, summing exactly to the pool, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- task placement (tasks to nodes by spare capacity, largest remainder) in FORTRAN as Trebago + APL as KRON witnessed
- "within the agent's budget" in Ada as Eve + APL as KRON witnessed
- task matrix in APL as KRON
- colony tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `largest_remainder` | num | `pool w1 w2 w3 w4` | sutra | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id skep
matrix 185
name Skep
needs swarm
needs peers
needs net
needs ledger.read
needs ledger.post
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

