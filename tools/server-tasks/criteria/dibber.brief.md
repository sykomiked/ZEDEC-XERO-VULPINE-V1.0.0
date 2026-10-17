# dibber — the complete brief

| | |
|---|---|
| matrix layer | 149 |
| icon group | L2 |
| wave spec | `wave12/149-dibber.md` |

## Purpose

plans plantings, watering and harvests for a plot from weather and land budgets, kept in the Hoard (`files.read`, `files.write`), for where the plot is (`place.here`). zxvd has no method for a weather source (as Weatherglass reports), soil sensor readings: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `location`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | seed and supply costs | primary | runs |
| FORTRAN | `trebago` | numeric | growth model | primary | runs |
| Ada | `eve` | limits | water limits | primary | runs |
| APL | `kron` | arrays | bed grid | primary | runs |
| MUMPS | `myra` | records | plot hierarchy | primary | runs |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Oberon | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Simula 67): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- water per bed per week (bed area in square decimetres x depth in mm = litres) exact in FORTRAN as Trebago + APL as KRON witnessed
- "may water now" (within the week's water budget AND no frost warning) in Ada as Eve + APL as KRON witnessed
- water limits in Ada as Eve
- seed and supply costs in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- days to harvest from growing degree days (sum of max(0, mean tenths - base)) exact in FORTRAN as Trebago
- bed grid (crop family per bed per year, no repeat on the same bed) in APL as KRON
- plot hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## The manifest contract

```
zxapp 1
id dibber
matrix 149
name Dibber
needs fs.read
needs fs.write
needs location
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

