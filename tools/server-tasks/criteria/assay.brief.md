# assay — the complete brief

| | |
|---|---|
| matrix layer | 72 |
| icon group | L1 |
| wave spec | `wave2/072-assay.md` |

## Purpose

run test prompts against the swarm's agents and score them (`swarm.state` agents and audit, `swarm.ask` under the budget gate, `play.run` for scored templates on scratch state).

## Capabilities (the exact registry names for `needs`)

- `swarm`
- `ai.ask`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| APL | `kron` | statistics | statistics | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |
| Racket | `-` | symbolic | symbolic reasoning | primary | planned: needs Lingua front-end |
| S | `-` | statistics | statistics | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- pass share x100 per agent in kron + trebago witnessed
- "meets the bar" truth (passes, refusals, minimum runs) in eve + kron witnessed
- a run's four counts (TRUE FALSE BOTH NEITHER) decided with `use pce_prob 1` (`verdict`, C)
- ranking over the score vector in kron. Run log stays in the C model (bounded ring)

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |

## The manifest contract

```
zxapp 1
id assay
matrix 72
name Assay
needs swarm
needs ai.ask
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

