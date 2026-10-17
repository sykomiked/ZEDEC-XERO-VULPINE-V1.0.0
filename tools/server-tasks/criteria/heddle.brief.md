# heddle — the complete brief

| | |
|---|---|
| matrix layer | 162 |
| icon group | L2 |
| wave spec | `wave13/162-heddle.md` |

## Purpose

designs woven and printed patterns and sends them to printers (`print.status`, `print.submit`, `print.job`) or looms as exact instructions, kept in the Hoard (`files.read`, `files.write`). zxvd has no method for loom output: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `print`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | yarn and cost sheet | primary | runs |
| FORTRAN | `trebago` | numeric | tension math | primary | runs |
| Ada | `eve` | limits | machine limits | primary | runs |
| APL | `kron` | arrays | weave matrix | primary | runs |
| MUMPS | `myra` | records | pattern tree | primary | runs |
| APT | `-` | toolpath | tool paths | primary | planned: needs Lingua front-end |
| PostScript | `-` | layout | page layout | primary | planned: needs Lingua front-end |
| Perl | `-` | pattern | patterns | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (APT): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- drawdown (threading x tie-up x treadling, as an integer matrix product, cell > 0 is a raised end) in APL as KRON + FORTRAN as Trebago witnessed
- yarn length per colour and cost in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- ends (ends per 10 cm x width) and picks exact in FORTRAN as Trebago
- "fits the machine" (shafts, width, colours at most the loom's or printer's) in Ada as Eve + APL as KRON witnessed
- pattern tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id heddle
matrix 162
name Heddle
needs print
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

