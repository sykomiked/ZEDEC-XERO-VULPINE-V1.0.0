# cirque — the complete brief

| | |
|---|---|
| matrix layer | 227 |
| icon group | L2 |
| wave spec | `wave18/227-cirque.md` |

## Purpose

Draws buildings from plan to 3D, checks measurements and produces drawing sets: projects in the Hoard (`files.read`, `files.write`), sets printed (`print.submit`, `print.status`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `print`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | project records | primary | runs |
| FORTRAN | `trebago` | numeric | structural loads | primary | runs |
| Ada | `eve` | limits | code limits | primary | runs |
| APL | `kron` | arrays | dimension grid | primary | runs |
| MUMPS | `myra` | records | building element tree | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |
| Julia | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| Bash | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.
- **-** (Julia): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "dimensions close" (each dimension string sums to its overall, in mm) in APL as KRON + FORTRAN as Trebago witnessed
- - (critical) code limits (room areas, stair rise and going, door widths from the owner's integer table) in Ada as Eve + APL as KRON witnessed
- - structural loads (dead plus live load per span in N/m, integers) in FORTRAN as Trebago, advisory
- - dimension grid (gridlines x levels) in APL as KRON
- - project records (sheet register, revision counts, refusing on overflow) in COBOL as Sutra
- - building element tree (level, room, wall, opening) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): designer A draws a wall at 4200 mm and designer B at 4350 mm, giving BOTH
- the test asserts state 3 and that both versions are kept and highlighted, neither overwritten. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id cirque
matrix 227
name Cirque
needs fs.read
needs fs.write
needs print
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

