# calliope — the complete brief

| | |
|---|---|
| matrix layer | 374 |
| icon group | L3 |
| wave spec | `wave31/374-calliope.md` |

## Purpose

Children create illustrated stories and games with a gentle AI helper in a safe, parent-guided space: stories in the Hoard (`files.read`, `files.write`), helper suggestions from `swarm.ask` (held for the child and parent), time from `screen.check` and `screen.status`, the family's seats (`seats.list`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ai.ask`
- `screen.time`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | allowance credits | primary | runs |
| FORTRAN | `trebago` | numeric | animation timing | primary | runs |
| Ada | `eve` | limits | screen-time limits | primary | runs |
| APL | `kron` | arrays | story grid | primary | runs |
| MUMPS | `myra` | records | family tree | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Pascal | `pascal` | stepwise | step-by-step teaching | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **pascal** (Pascal): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `help_ok`: (critical) "show this helper suggestion" (age-appropriate AND within the child's AI-help limit AND parent allowed AI help) in Ada as Eve + APL as KRON witnessed
- - `time_ok`: (critical) screen-time limits for the studio in Ada as Eve + APL as KRON witnessed
- - `timing`: animation timing (frames per scene at a rate, exact) in FORTRAN as Trebago
- - `steps`: story-making steps one at a time in BASIC (twin Hatchling)
- - `story`: story grid in APL as KRON
- - `allowance`: allowance credits (credits for finished stories, refusing on overflow) in COBOL as Sutra
- - `family`: family tree in MUMPS as MYRA
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): the child and the helper want different endings: the ending is BOTH
- the test asserts state 3 and that both endings are kept. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id calliope
matrix 374
name Calliope
needs fs.read
needs fs.write
needs ai.ask
needs screen.time
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

