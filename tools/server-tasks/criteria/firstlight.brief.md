# firstlight — the complete brief

| | |
|---|---|
| matrix layer | 304 |
| icon group | L3 |
| wave spec | `wave25/304-firstlight.md` |

## Purpose

a child's first computer: safe play, reading, puzzles and first coding lessons with parents in charge (`screen.check`, `screen.status`, `files.read`, `files.write`, `play.catalogue`, `play.run`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `screen.time`
- `sys.read`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | pocket-money ledger | primary | runs |
| FORTRAN | `trebago` | numeric | learning pace model | primary | runs |
| Ada | `eve` | limits | screen time limits | primary | runs |
| APL | `kron` | arrays | puzzle grids | primary | runs |
| MUMPS | `myra` | records | child profile tree | primary | runs |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |

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

- `screen_ok` (minutes left today AND inside the parents' hours) in Ada as Eve + APL as KRON witnessed
- `pace` (lessons a week at the child's recent pace) in FORTRAN as Trebago + APL as KRON witnessed
- `pocket_money` (a play ledger in minor units, never real money) in COBOL as Sutra
- `puzzles` grids in APL as KRON
- first coding steps in BASIC (twin Hatchling)
- `child_profile` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## The manifest contract

```
zxapp 1
id firstlight
matrix 304
name Firstlight
needs fs.read
needs fs.write
needs screen.time
needs sys.read
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

