# spyglass — the complete brief

| | |
|---|---|
| matrix layer | 117 |
| icon group | L1 |
| wave spec | `wave10/117-spyglass.md` |

## Purpose

magnify the area around the pointer or focus, full screen or lens. Screen size from `sys.info`; zxvd has no display-capture or accessibility-settings method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Dart | `dart` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |
| Laning and Zierler | `-` | formula | formulas | primary | planned: needs Lingua front-end |
| Raku | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **dart** (Dart): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Laning and Zierler): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- source rectangle for a pointer and zoom (q8, 1x..16x, kept inside the screen) in kron + trebago witnessed
- focus-follow step (at most N pixels a frame toward the target) in kron

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id spyglass
matrix 117
name Spyglass
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

