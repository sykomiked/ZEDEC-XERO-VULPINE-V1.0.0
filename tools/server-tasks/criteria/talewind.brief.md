# talewind — the complete brief

| | |
|---|---|
| matrix layer | 108 |
| icon group | L1 |
| wave spec | `wave9/108-talewind.md` |

## Purpose

stories in the Banker (`files.read`, `files.write`); AI-drafted passages from `swarm.ask`.

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
| Pascal | `pascal` | stepwise | step-by-step teaching | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier
- **pascal** (Pascal): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- "choice shown" (required flags held, age rating allowed) in kron + eve witnessed
- story graph (passages, choices, flags) in myra
- AI-drafted passages are held for the author's approval

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id talewind
matrix 108
name Talewind
needs fs.read
needs fs.write
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

