# bodkin — the complete brief

| | |
|---|---|
| matrix layer | 183 |
| icon group | L2 |
| wave spec | `wave15/183-bodkin.md` |

## Purpose

turns body measurements into fitted sewing patterns and cut sheets in the Hoard (`files.read`, `files.write`), printed with `print.submit`.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `print`
- `ledger.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | order book | primary | runs |
| FORTRAN | `trebago` | numeric | fit geometry | primary | runs |
| Ada | `eve` | limits | size limits | primary | runs |
| APL | `kron` | arrays | measurement grid | primary | runs |
| MUMPS | `myra` | records | garment tree | primary | runs |
| APT | `-` | toolpath | tool paths | primary | planned: needs Lingua front-end |
| SNOBOL | `-` | pattern | patterns | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (APT): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- piece size (measurement + ease, graded between sizes, millimetres) in FORTRAN as Trebago + APL as KRON witnessed
- "measurements plausible" (each within its declared range) in Ada as Eve + APL as KRON witnessed
- order book (order number, price x quantity in minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- measurement grid in APL as KRON
- garment tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id bodkin
matrix 183
name Bodkin
needs fs.read
needs fs.write
needs print
needs ledger.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

