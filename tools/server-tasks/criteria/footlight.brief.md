# footlight — the complete brief

| | |
|---|---|
| matrix layer | 41 |
| icon group | L1 |
| wave spec | `wave6/041-footlight.md` |

## Purpose

decks as files (`files.read`, `files.write`), printed handouts (`print.submit`), presented to others in a watch hall (`pspace.create`, `hall.load`, `hall.seek`, `hall.status`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `print`
- `peers`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Icon | `-` | text | text | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |
| PostScript | `-` | layout | page layout | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **-** (Icon): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- slide scale to fit the window (16:9 into any size, letterbox) in kron + C++ wasm witnessed
- rehearsal timing (seconds per slide, total against talk length) exact in trebago
- run order (advance, hold, loop) in basic
- slide record in myra

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id footlight
matrix 41
name Footlight
needs fs.read
needs fs.write
needs print
needs peers
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

