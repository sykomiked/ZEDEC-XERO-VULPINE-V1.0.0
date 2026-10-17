# caldera — the complete brief

| | |
|---|---|
| matrix layer | 259 |
| icon group | L2 |
| wave spec | `wave21/259-caldera.md` |

## Purpose

Watches natural hazards and pushes local alerts: events (`journal.query`, `log.tail`), the place (`place.here`), local alerts to the mesh only after a person confirms (`chat.post`, `pspace.post`), neighbours' nodes (`net.peers`, `devmesh.devices`). Hazard sensor feeds have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `journal`
- `location`
- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | incident records | primary | runs |
| FORTRAN | `trebago` | numeric | seismic model | primary | runs |
| Ada | `eve` | limits | evacuation thresholds | primary | runs |
| APL | `kron` | arrays | hazard grid | primary | runs |
| MUMPS | `myra` | records | region tree | primary | runs |
| Elixir | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "raise a local alert" (a reading at or above the evacuation threshold for this region) in Ada as Eve + APL as KRON witnessed, held for a person's confirm
- - (critical) local magnitude x10 from amplitude and distance by table in FORTRAN as Trebago + APL as KRON witnessed
- - hazard grid (region cells x hazard levels) in APL as KRON
- - incident records (incident numbers and counts, refusing on overflow) in COBOL as Sutra
- - region tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): sensor A raises an alert and sensor B shows none, giving BOTH
- the test asserts state 3 and a "prepare" warning, never an all-clear. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id caldera
matrix 259
name Caldera
needs journal
needs location
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

