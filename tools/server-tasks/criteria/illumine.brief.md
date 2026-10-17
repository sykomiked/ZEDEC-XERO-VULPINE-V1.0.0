# illumine — the complete brief

| | |
|---|---|
| matrix layer | 313 |
| icon group | L3 |
| wave spec | `wave26/313-illumine.md` |

## Purpose

publishes books from manuscript to finished edition: editing, layout, illustration, rights and sales (`files.read`, `files.write`, `print.submit`, `aipi.put`, `web4.cid`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `print`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | royalties | primary | runs |
| FORTRAN | `trebago` | numeric | print cost model | primary | runs |
| Ada | `eve` | limits | rights limits | primary | runs |
| APL | `kron` | arrays | layout grid | primary | runs |
| MUMPS | `myra` | records | chapter and edition tree | primary | runs |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |
| Lua | `-` | scripting | scripting | primary | planned: needs Lingua front-end |
| AWK | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (TeX): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `rights_ok` (each image and passage used within its recorded rights and territory) in Ada as Eve + APL as KRON witnessed
- `print_cost` (pages, paper and binding per copy) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `royalties` (shares per contributor, largest remainder, in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `layout` grid in APL as KRON
- `editions` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `largest_remainder` | num | `pool w1 w2 w3 w4` | sutra | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |

## The manifest contract

```
zxapp 1
id illumine
matrix 313
name Illumine
needs fs.read
needs fs.write
needs print
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

