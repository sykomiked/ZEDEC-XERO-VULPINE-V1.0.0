# galliard — the complete brief

| | |
|---|---|
| matrix layer | 390 |
| icon group | L3 |
| wave spec | `wave32/390-galliard.md` |

## Purpose

Trains martial arts, dance or movement with pose capture, drills, grading and injury-safe limits: sessions in the Hoard (`files.read`, `files.write`), the clock (`sys.info`), classes shown in a hall (`hall.load`, `hall.play`, `hall.status`). Pose capture from the camera has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `camera`
- `fs.read`
- `fs.write`
- `sys.read`
- `peers`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | dojo fees | primary | runs |
| FORTRAN | `trebago` | numeric | pose geometry | primary | runs |
| Ada | `eve` | limits | joint angle limits | primary | runs |
| APL | `kron` | arrays | motion arrays | primary | runs |
| MUMPS | `myra` | records | student tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- - `joint_ok`: (critical) joint angle limits (each angle within the student's declared range) in Ada as Eve + APL as KRON witnessed
- - `pose`: (critical) pose geometry (angles from key points with an integer arctangent table) in FORTRAN as Trebago + APL as KRON witnessed
- - `motion`: motion arrays in APL as KRON
- - `fees`: dojo fees (minor units, refusing on overflow) in COBOL as Sutra
- - `students`: student tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the camera grades a form 7 and the instructor 9: the grade is BOTH
- the test asserts state 3 and that both stand until reviewed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id galliard
matrix 390
name Galliard
needs camera
needs fs.read
needs fs.write
needs sys.read
needs peers
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

