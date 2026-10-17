# elevation — the complete brief

| | |
|---|---|
| matrix layer | 301 |
| icon group | L3 |
| wave spec | `wave25/301-elevation.md` |

## Purpose

designs buildings from sketch to construction drawings, with structure checks, materials and cost (`files.read`, `files.write`, `print.submit`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `print`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | cost estimates | primary | runs |
| FORTRAN | `trebago` | numeric | structural analysis | primary | runs |
| Ada | `eve` | limits | building code limits | primary | runs |
| APL | `kron` | arrays | drawing grids | primary | runs |
| MUMPS | `myra` | records | building element tree | primary | runs |
| Julia | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| JCL | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Julia): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- `code_ok` (each declared code clause met: every clause's result joined with `lpres_conjoin` in C, KRON counting clauses per value) with `clause_check` (each clause as an integer rule) in Ada as Eve + APL as KRON witnessed
- `structure` (member forces and deflection, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `cost_estimate` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `drawings` grid in APL as KRON
- `elements` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |

## The manifest contract

```
zxapp 1
id elevation
matrix 301
name Elevation
needs fs.read
needs fs.write
needs print
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

