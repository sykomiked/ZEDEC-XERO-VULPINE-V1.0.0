# rainshadow — the complete brief

| | |
|---|---|
| matrix layer | 261 |
| icon group | L2 |
| wave spec | `wave21/261-rainshadow.md` |

## Purpose

Plans sky watching: eclipses and transits, where and when, from the place (`place.here`, `place.ask`) and the clock (`sys.info`); photo plans and room (`files.write`, `sys.disk`).

## Capabilities (the exact registry names for `needs`)

- `location`
- `sys.read`
- `fs.write`
- `camera`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | observation log | primary | runs |
| FORTRAN | `trebago` | numeric | orbital model | primary | runs |
| Ada | `eve` | limits | eye safety limits | primary | runs |
| APL | `kron` | arrays | ephemeris grid | primary | runs |
| MUMPS | `myra` | records | event tree | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Pascal | `pascal` | stepwise | step-by-step teaching | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier
- **pascal** (Pascal): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "safe to look without a filter" (only inside the totality window at this place
- every partial phase needs a certified filter) in Ada as Eve + APL as KRON witnessed
- - (critical) contact times at this place (seconds) from the integer ephemeris tables in FORTRAN as Trebago + APL as KRON witnessed
- - ephemeris grid (events x places) in APL as KRON
- - observation log and event tree in MUMPS as MYRA
- - Sutra is left out: no money or counted business records here
- Paraconsistent (BOTH test, in test_zxapp.c): model A gives first contact at 14:03:20 and model B at 14:04:10, giving BOTH
- the test asserts state 3 and that both times are shown with their margins. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id rainshadow
matrix 261
name Rainshadow
needs location
needs sys.read
needs fs.write
needs camera
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

