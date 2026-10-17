# crown — the complete brief

| | |
|---|---|
| matrix layer | 75 |
| icon group | L1 |
| wave spec | `wave3/075-crown.md` |

## Purpose

mutual endowment and distribution by sortition over the grant engine (`grants.status`, `grants.eval`, `grants.epoch`, `grants.pool_credit`, `grants.distribute`, `grants.account`). There is no draw method: use only existing methods; list what's missing in your report (a seeded, verifiable sortition draw).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | money and ledgers | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| Go | `go` | concurrency | concurrency | primary | planned: SDK host only, no in-app runtime yet |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Eiffel): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "in good standing" from `grants.eval`'s reason bits (no unappealed bit set) in eve + sutra witnessed
- each recipient's grant split by weight, largest remainder, summing exactly to the pool, in sutra + trebago witnessed
- draw order from a published seed and member count (integer modular steps, no hashing in the block) in trebago + sutra witnessed
- pool and epoch totals exact in trebago

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id crown
matrix 75
name Crown
needs ledger.read
needs ledger.post
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

