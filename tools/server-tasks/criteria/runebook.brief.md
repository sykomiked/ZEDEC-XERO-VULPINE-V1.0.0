# runebook — the complete brief

| | |
|---|---|
| matrix layer | 114 |
| icon group | L1 |
| wave spec | `wave10/114-runebook.md` |

## Purpose

every character, emoji and symbol by script and block, with favourites saved in the Hoard (`files.read`, `files.write`); language names from `xlate.languages`. zxvd has no character-data method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Scheme | `scheme` | symbolic | symbolic reasoning | primary | runs |
| CPL | `-` | formula | formulas | primary | planned: needs Lingua front-end |
| Icon | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **scheme** (Scheme): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (CPL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- valid code point (0..0x10FFFF, not a surrogate, not a noncharacter) in kron + trebago witnessed
- UTF-8 length and bytes of a code point in trebago + kron witnessed
- block of a code point (search over the block-start vector) and recently used counts in kron

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id runebook
matrix 114
name Runebook
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

