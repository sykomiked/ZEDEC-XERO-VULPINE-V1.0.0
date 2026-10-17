# descant — the complete brief

| | |
|---|---|
| matrix layer | 397 |
| icon group | L3 |
| wave spec | `wave33/397-descant.md` |

## Purpose

Grows a personal learning tree: goals and lessons in the Hoard (`files.read`, `files.write`, `search.query`), lessons run (`play.catalogue`, `play.run`), credentials signed by a teacher and checked (`web4.verify`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `ai.ask`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | credential records | primary | runs |
| FORTRAN | `trebago` | numeric | mastery estimates | primary | runs |
| Ada | `eve` | limits | pace limits | primary | runs |
| APL | `kron` | arrays | skill grid | primary | runs |
| MUMPS | `myra` | records | learning tree | primary | runs |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |
| ML | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | witness | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **basic** (BASIC): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (ML): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `credential_ok`: (critical) "issue this credential" (every required skill mastered AND a teacher signed) in Ada as Eve + APL as KRON witnessed
- - `mastery`: (critical) mastery estimate (right answers in a row and spaced reviews, integer score) in FORTRAN as Trebago + APL as KRON witnessed
- - `pace_ok`: pace limits (lessons per day within the learner's declared pace) in Ada as Eve
- - `steps`: the next step shown one at a time in BASIC (twin Hatchling)
- - `skills`: skill grid in APL as KRON
- - `records`: credential records (refusing on overflow) in COBOL as Sutra
- - `tree`: learning tree in MUMPS as MYRA
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): the tests mark a skill mastered and the teacher does not: it is BOTH
- the test asserts state 3 and that both are recorded and no credential is issued yet. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id descant
matrix 397
name Descant
needs fs.read
needs fs.write
needs search
needs ai.ask
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

