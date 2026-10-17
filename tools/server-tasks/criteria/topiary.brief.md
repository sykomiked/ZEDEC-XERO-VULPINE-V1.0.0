# topiary — the complete brief

| | |
|---|---|
| matrix layer | 204 |
| icon group | L2 |
| wave spec | `wave16/204-topiary.md` |

## Purpose

shapes habits over months with small daily tasks and reminders in the Hoard (`files.read`, `files.write`) and the companion's nudges (`companion.mood`, `swarm.ask`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `notify`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | habit streaks | primary | runs |
| FORTRAN | `trebago` | numeric | progress trend | primary | runs |
| Ada | `eve` | limits | rest limits | primary | runs |
| APL | `kron` | arrays | day grid | primary | runs |
| MUMPS | `myra` | records | goal tree | primary | runs |
| IPL | `-` | lists | lists | primary | planned: needs Lingua front-end |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (IPL): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- streak length (days done in a row, a BOTH day counts as not broken) in APL as KRON + FORTRAN as Trebago witnessed
- "may nudge now" (at most the daily count, not in quiet hours, rest days honoured) in Ada as Eve + APL as KRON witnessed
- weekly trend exact in FORTRAN as Trebago
- day grid in APL as KRON
- goal tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id topiary
matrix 204
name Topiary
needs fs.read
needs fs.write
needs notify
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

