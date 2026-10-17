# broadsheet — the complete brief

| | |
|---|---|
| matrix layer | 42 |
| icon group | L1 |
| wave spec | `wave6/042-broadsheet.md` |

## Purpose

open documents from the Hoard (`files.read`, base64), print (`print.submit`); annotations go to a separate file beside the original (`files.write`), never into it.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `print`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Perl | `-` | text | text | primary | planned: needs Lingua front-end |
| PostScript | `-` | layout | page layout | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **-** (Perl): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "well formed" (header present, every cross-reference offset inside the file size) in Rust wasm + eve witnessed, so a malformed file is refused
- zoom to fit page in kron
- annotation record in myra

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id broadsheet
matrix 42
name Broadsheet
needs fs.read
needs fs.write
needs print
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

