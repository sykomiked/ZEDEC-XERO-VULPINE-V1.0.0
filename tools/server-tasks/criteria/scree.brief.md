# scree — the complete brief

| | |
|---|---|
| matrix layer | 219 |
| icon group | L2 |
| wave spec | `wave18/219-scree.md` |

## Purpose

Builds and plays logic and maths puzzles that adapt to a learner's level; puzzles and progress in the Hoard (`files.read`, `files.write`), hints from `swarm.ask` held until the learner asks, time from `screen.check`.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ai.ask`
- `screen.time`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | score records | primary | runs |
| FORTRAN | `trebago` | numeric | difficulty curve | primary | runs |
| Ada | `eve` | limits | hint limits | primary | runs |
| APL | `kron` | arrays | puzzle grid | primary | runs |
| MUMPS | `myra` | records | learner progress tree | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |
| JCL | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **basic** (BASIC): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (JCL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) next level from the last 10 results (up when the correct share x10000 is at least 8000, down at 4000 or less, else the same) in FORTRAN as Trebago + APL as KRON witnessed
- - (critical) hints left on a puzzle (0..3, none once solved) in Ada as Eve + APL as KRON witnessed
- - puzzle grid (generate a grid from a seed and a level, every constraint over rows and columns) in APL as KRON
- - score records (points per puzzle and the running total, refusing on overflow) in COBOL as Sutra
- - learner progress tree (learner, topic, level, results) in MUMPS as MYRA
- - the worked solution shown one step at a time in BASIC (twin Hatchling)
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): a 4x4 grid with two valid fillings: the stored answer differs from the learner's (FALSE) while Riddle's "solved" rule passes (TRUE), giving BOTH
- the test asserts state 3 and that the answer is accepted as correct ("another valid answer"), never marked wrong. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id scree
matrix 219
name Scree
needs fs.read
needs fs.write
needs ai.ask
needs screen.time
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

