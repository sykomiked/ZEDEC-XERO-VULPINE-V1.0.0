# sapling — the complete brief

| | |
|---|---|
| matrix layer | 98 |
| icon group | L1 |
| wave spec | `wave8/098-sapling.md` |

## Purpose

courses kept in the Banker (`files.read`, `files.write`); coding lessons run through `play.catalogue` and `play.run`.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **basic** (BASIC): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- "lesson unlocked" (every prerequisite passed) in kron + basic witnessed
- progress x100 and quiz scores in kron
- courses and results in myra

## The entries and their exact checks

(no library pattern matched; author the entries from the clauses)

## The manifest contract

```
zxapp 1
id sapling
matrix 98
name Sapling
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

