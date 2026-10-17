# quarry — the complete brief

| | |
|---|---|
| matrix layer | 69 |
| icon group | L1 |
| wave spec | `wave7/069-quarry.md` |

## Purpose

search the Hoard (`search.query`, `files.read`), the owner's Web4 feed (`web4.feed`), summarize with the companion (`swarm.ask`). Searching the web has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `search`
- `fs.read`
- `ai.ask`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| MUMPS | `myra` | index | index | primary | runs |
| dBase | `-` | query | queries | primary | planned: needs Lingua front-end |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (dBase): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.

## The spec's Logic clauses (what each component computes)

- "this claim has a source" (each claim names a held citation) in eve + kron witnessed
- sources per topic in kron + trebago
- summary length limits in eve
- citation record in myra

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id quarry
matrix 69
name Quarry
needs search
needs fs.read
needs ai.ask
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

