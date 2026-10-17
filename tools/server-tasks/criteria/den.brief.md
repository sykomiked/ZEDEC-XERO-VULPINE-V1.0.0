# den — the complete brief

| | |
|---|---|
| matrix layer | 97 |
| icon group | L1 |
| wave spec | `wave4/097-den.md` |

## Purpose

installed games in the Banker (`files.list`) and time left today (`screen.check`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `screen.time`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier
- **basic** (BASIC): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- "may this game start now" (minutes left, age rating at or below the seat's) in eve + kron witnessed
- play time per game and sorting in kron
- the built-in classic type-in games' turn logic in basic
- library in myra

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## The manifest contract

```
zxapp 1
id den
matrix 97
name Den
needs fs.read
needs screen.time
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

