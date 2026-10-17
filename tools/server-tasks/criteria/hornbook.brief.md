# hornbook — the complete brief

| | |
|---|---|
| matrix layer | 169 |
| icon group | L2 |
| wave spec | `wave14/169-hornbook.md` |

## Purpose

builds lessons with diagrams, slides and flashcards in the Hoard (`files.read`, `files.write`), then tracks each learner's progress.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | enrolment records | primary | runs |
| FORTRAN | `trebago` | numeric | score statistics | primary | runs |
| Ada | `eve` | limits | age-appropriate limits | primary | runs |
| APL | `kron` | arrays | mastery grid | primary | runs |
| MUMPS | `myra` | records | learner tree | primary | runs |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Bash | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **basic** (BASIC): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "age-appropriate" (the lesson's band at most the learner's) in Ada as Eve + APL as KRON witnessed
- next lesson step from the learner's answer in BASIC (twin Hatchling) + APL as KRON witnessed
- enrolment records (learner, class, seat
- seats per class refused on overflow) in COBOL as Sutra
- score statistics (mean floor, min, max per question) exact in FORTRAN as Trebago
- mastery grid (learner by skill, mastered after k right in a row) in APL as KRON
- learner tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id hornbook
matrix 169
name Hornbook
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

