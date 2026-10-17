# tephra — the complete brief

| | |
|---|---|
| matrix layer | 328 |
| icon group | L3 |
| wave spec | `wave27/328-tephra.md` |

## Purpose

monitors volcanoes, earthquakes and landslides, turning sensor data into hazard maps and evacuation advice (`journal.query`, `log.tail`, `place.here`, `net.peers`, `pspace.post`). zxvd has no method for hazard sensor feeds: use only existing methods; list what's missing in your report.

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
| COBOL | `sutra` | money | response budget | primary | runs |
| FORTRAN | `trebago` | numeric | seismic models | primary | runs |
| Ada | `eve` | limits | evacuation thresholds | primary | runs |
| APL | `kron` | arrays | sensor arrays | primary | runs |
| MUMPS | `myra` | records | zone hierarchy | primary | runs |
| JOVIAL | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Clojure | `-` | dataflow | dataflow | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (JOVIAL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `evacuate_advice` (readiness level from the declared thresholds
- an evacuation advice needs two instruments over threshold) in Ada as Eve + APL as KRON witnessed
- `seismic` (event magnitude from integer amplitude tables) in FORTRAN as Trebago + APL as KRON witnessed
- `response_budget` (in minor units) in COBOL as Sutra
- `sensors` arrays in APL as KRON
- `zones` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id tephra
matrix 328
name Tephra
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

