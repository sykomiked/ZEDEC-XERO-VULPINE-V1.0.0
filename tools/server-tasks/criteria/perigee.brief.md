# perigee — the complete brief

| | |
|---|---|
| matrix layer | 286 |
| icon group | L3 |
| wave spec | `wave24/286-perigee.md` |

## Purpose

plans drone deliveries and inspections within declared airspace, weather and battery limits, from places (`place.here`, `place.list`) and station data (`devmesh.devices`), with plans in the Hoard (`files.read`, `files.write`). zxvd has no method for drone control and telemetry (as Tiller reports), airspace notices: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `sys.read`
- `net`
- `peers`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | delivery charges | primary | runs |
| FORTRAN | `trebago` | numeric | flight path model | primary | runs |
| Ada | `eve` | limits | altitude and battery limits | primary | runs |
| APL | `kron` | arrays | airspace grid | primary | runs |
| MUMPS | `myra` | records | fleet records | primary | runs |
| Ladder logic | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Delphi | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Ladder logic): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `flight_ok` (altitude within the declared band AND battery at return at least the reserve AND wind at most the limit AND route outside every no-fly cell) in Ada as Eve + APL as KRON witnessed
- `flight_path` (distance and energy along the legs) in FORTRAN as Trebago + APL as KRON witnessed
- `delivery_charge` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `airspace` grid in APL as KRON
- `fleet` records in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id perigee
matrix 286
name Perigee
needs location
needs sys.read
needs net
needs peers
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

