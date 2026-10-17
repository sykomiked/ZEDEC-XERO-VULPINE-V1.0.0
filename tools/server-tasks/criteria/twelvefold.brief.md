# twelvefold — the complete brief

| | |
|---|---|
| matrix layer | 132 |
| icon group | L1 |
| wave spec | `wave3/132-twelvefold.md` |

## Purpose

write formulas over TRUE, FALSE, BOTH, NEITHER, see their tables and the 64-state hexagram tag; try the engines' Playground templates with `play.run`; save formulas in the Hoard (`files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Lua | `-` | scripting | scripting | primary | planned: needs Lingua front-end |
| Clojure | `-` | symbolic | symbolic reasoning | primary | planned: needs Lingua front-end |
| Raku | `-` | parse | parsing | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **-** (Lua): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- evaluation stays C through `lpres_conjoin` / `lpres_disjoin` (kernel order, never re-derived
- say so in the manifest)
- assignments per resulting value counted in kron
- "conclusion follows" (every assignment where the premises are TRUE or BOTH gives the conclusion TRUE or BOTH, over the value codes C produced) in eve + kron witnessed
- limits (at most 6 variables, depth at most 16) in eve

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id twelvefold
matrix 132
name Twelvefold
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

