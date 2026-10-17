# heirloom — the complete brief

| | |
|---|---|
| matrix layer | 81 |
| icon group | L1 |
| wave spec | `wave8/081-heirloom.md` |

## Purpose

the inheritance manifest, guardians and recovery (`estate.setup`, `estate.manifest`, `estate.faraid`, `estate.distribute`, `estate.status`, `recovery.setup`, `recovery.status`, `recovery.claim`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | money and ledgers | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| Ada | `eve` | safety | safety rules | primary | runs |
| Elixir | `-` | faults | fault handling | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Oberon | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- manifest fractions sum to exactly 1 (cross-multiplied) in sutra + trebago witnessed
- split preview by largest remainder in sutra + trebago witnessed, and compared with `estate.distribute` (reports only)
- "release due" (k guardian approvals and the waiting period over, no living veto) in eve + trebago witnessed
- Nothing happens on time or silence alone
- owner and guardians sign. Tests make their own throwaway keys, never anything from build_system/keys/

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `largest_remainder` | num | `pool w1 w2 w3 w4` | sutra | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id heirloom
matrix 81
name Heirloom
needs ledger.read
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

