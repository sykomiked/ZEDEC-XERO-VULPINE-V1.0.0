# lore — the complete brief

| | |
|---|---|
| matrix layer | 67 |
| icon group | L1 |
| wave spec | `wave7/067-lore.md` |

## Purpose

the owner's notes and files (`files.list`, `files.read`, `search.query`, `search.refresh`), read by the companion (`swarm.ask`) only with leave.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `search`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |
| Mathematica | `-` | symbolic | symbolic reasoning | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **-** (Occam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may the AI read this file" (leave given, folder granted, not from a more private space) in eve + kron witnessed
- days left on a leave in kron
- leave record in myra

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id lore
matrix 67
name Lore
needs fs.read
needs search
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

