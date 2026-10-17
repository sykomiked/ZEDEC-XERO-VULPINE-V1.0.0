# riddle — the complete brief

| | |
|---|---|
| matrix layer | 104 |
| icon group | L1 |
| wave spec | `wave9/104-riddle.md` |

## Purpose

logic and number puzzles; saves in the Banker (`files.read`, `files.write`); hints from `swarm.ask`.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier
- **basic** (BASIC): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "solved" (every row, column and box holds each value once) in kron + basic witnessed
- daily puzzle pick from the date in kron
- puzzles and times in myra

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id riddle
matrix 104
name Riddle
needs fs.read
needs fs.write
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

