# alembic — the complete brief

| | |
|---|---|
| matrix layer | 155 |
| icon group | L2 |
| wave spec | `wave12/155-alembic.md` |

## Purpose

logs experiments, recipes and results as a lab notebook in the Hoard (`files.read`, `files.write`, `search.query`) with exact integer calculations.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | reagent stock | primary | runs |
| FORTRAN | `trebago` | numeric | reaction math | primary | runs |
| Ada | `eve` | limits | hazard limits | primary | runs |
| APL | `kron` | arrays | result tables | primary | runs |
| MUMPS | `myra` | records | experiment tree | primary | runs |
| AWK | `-` | text | text | primary | planned: needs Lingua front-end |
| ALGOL 68 | `-` | formula | formulas | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (AWK): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- amount of substance (mass in micrograms x 1000 / molar mass, as a reduced rational) in FORTRAN as Trebago + APL as KRON witnessed
- reagent stock in milligrams, a withdrawal refused below zero or on overflow, in COBOL as Sutra + FORTRAN as Trebago witnessed
- "step allowed" (every reagent's hazard class among the bench's declared classes AND quantity at most the declared maximum) in Ada as Eve + APL as KRON witnessed
- result tables (result vector per run, difference from an earlier run against the tolerance) in APL as KRON
- experiment tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## The manifest contract

```
zxapp 1
id alembic
matrix 155
name Alembic
needs fs.read
needs fs.write
needs search
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

