# gnomon — the complete brief

| | |
|---|---|
| matrix layer | 107 |
| icon group | L1 |
| wave spec | `wave9/107-gnomon.md` |

## Purpose

exact shapes and numbers; saves in the Banker (`files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| F# | `dotnet` | numeric | exact numerics | primary | planned: SDK host only, no in-app runtime yet |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **dotnet** (F#): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- twice a polygon's area (shoelace, exact) and squared side lengths in kron + C++ wasm witnessed
- fractions reduced with `use number_classics 1`
- saved figures in myra. Turtle paths stay in C (Logo has no runtime)
- say so in the manifest

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |

## The manifest contract

```
zxapp 1
id gnomon
matrix 107
name Gnomon
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

