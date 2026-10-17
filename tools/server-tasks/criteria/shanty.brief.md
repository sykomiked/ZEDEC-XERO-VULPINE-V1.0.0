# shanty — the complete brief

| | |
|---|---|
| matrix layer | 362 |
| icon group | L3 |
| wave spec | `wave30/362-shanty.md` |

## Purpose

Forecasts tides, waves and coastal flooding for harbours and shore towns: districts (`place.here`, `place.list`), stations (`net.peers`, `devmesh.devices`), records (`files.read`, `files.write`), alerts sent once a person approves (`pspace.post`), history (`journal.query`). Gauge and buoy readings have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `peers`
- `net`
- `fs.read`
- `fs.write`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | harbour dues | primary | runs |
| FORTRAN | `trebago` | numeric | wave models | primary | runs |
| Ada | `eve` | limits | flood thresholds | primary | runs |
| APL | `kron` | arrays | tide grids | primary | runs |
| MUMPS | `myra` | records | coastline tree | primary | runs |
| Elixir | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Forth | `forth` | control | machine control | primary | runs |
| Clojure | `-` | dataflow | dataflow | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier
- **forth** (Forth): the run tier: stack words. '<entry> ( args -- result )' with integer words. Range laws are natural here.

## The spec's Logic clauses (what each component computes)

- - `flood_ok`: (critical) flood thresholds (forecast level plus wave run-up at or above a district's threshold) in Ada as Eve + APL as KRON witnessed
- - `waves`: (critical) wave models (significant height from wind speed, fetch and duration, integer tables) in FORTRAN as Trebago + APL as KRON witnessed
- - `buoy`: buoy raw counts to millimetres with a range check in Forth (twin Cairndrake)
- - `send_alert`: alert routing (district to channel) in APL as KRON
- - `tides`: tide grids in APL as KRON
- - `dues`: harbour dues (minor units, refusing on overflow) in COBOL as Sutra
- - `coast`: coastline tree in MUMPS as MYRA
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): two forecast models give 2.1 m and 2.9 m for the same high water: the forecast is BOTH
- the test asserts state 3 and that the range is shown and the district prepares for 2.9 m. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `within_range` | truth | `value min max` | eve | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id shanty
matrix 362
name Shanty
needs location
needs peers
needs net
needs fs.read
needs fs.write
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

