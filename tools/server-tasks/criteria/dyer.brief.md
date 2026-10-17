# dyer — the complete brief

| | |
|---|---|
| matrix layer | 116 |
| icon group | L1 |
| wave spec | `wave10/116-dyer.md` |

## Purpose

pick colours, convert between RGB, HSL and hex, check contrast, keep palettes in the Hoard (`files.read`, `files.write`). Picking from the screen has no zxvd method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| JOSS | `-` | formula | formulas | primary | planned: needs Lingua front-end |
| Perl | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **-** (JOSS): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- RGB to HSL and back in integers (hue 0..359, saturation and lightness 0..1000, channels 0..255 or refused) in kron + trebago witnessed
- contrast ratio x100 from relative luminance (integer sRGB table, scale 10^6) with the 450 and 300 pass marks in trebago + kron witnessed

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id dyer
matrix 116
name Dyer
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

