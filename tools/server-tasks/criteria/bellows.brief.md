# bellows — the complete brief

| | |
|---|---|
| matrix layer | 59 |
| icon group | L1 |
| wave spec | `wave7/059-bellows.md` |

## Purpose

requests through the companion (`swarm.ask`, `model.info`) under the owner's budgets (`power.budget`, `power.status`), results held until accepted (M3), then saved (`files.write`). Image and voice generation have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `ai.ask`
- `swarm`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| MATLAB | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **-** (MATLAB): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "within budget" in eve + kron witnessed
- token cost in sutra + kron witnessed (denominations from kernel/src/swarm/swarm_denom.h, refused on overflow)
- energy of a request (microjoules from `power.status`) exact in trebago
- request record in myra
- Never imitate a real person's voice or face without their recorded consent

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id bellows
matrix 59
name Bellows
needs ai.ask
needs swarm
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

