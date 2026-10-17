# hatchery — the complete brief

| | |
|---|---|
| matrix layer | 65 |
| icon group | L1 |
| wave spec | `wave7/065-hatchery.md` |

## Purpose

templates (`play.catalogue`), drafts from the swarm (`play.draft`, untrusted), runs on scratch state (`play.run`), publish and run (`aipi.publish`, `aipi.execute`), energy budgets (`power.budget`), the swarm (`swarm.state`).

## Capabilities (the exact registry names for `needs`)

- `swarm`
- `ai.ask`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Go | `go` | concurrency | concurrency | primary | planned: SDK host only, no in-app runtime yet |
| JCL | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (JCL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- agent limits (fuel_max, energy window, tokens per cycle) in eve + kron witnessed
- teaching example counts in trebago
- agent record in myra
- An agent runs only on the owner's approval. Joining `play.run`'s four-valued verdict with the other checks uses `lpres_conjoin` in C, never re-derived

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id hatchery
matrix 65
name Hatchery
needs swarm
needs ai.ask
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

