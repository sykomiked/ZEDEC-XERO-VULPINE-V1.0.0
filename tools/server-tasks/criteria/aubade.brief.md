# aubade — the complete brief

| | |
|---|---|
| matrix layer | 368 |
| icon group | L3 |
| wave spec | `wave31/368-aubade.md` |

## Purpose

Tracks daylight, UV and the sleep-wake rhythm to plan outdoor work and healthy light: the place (`place.here`), the clock (`sys.info`), logs in the Hoard (`files.read`, `files.write`), quiet hours (`screen.status`, `notes.list`). UV sensors have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `sys.read`
- `fs.read`
- `fs.write`
- `screen.time`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | shift pay | primary | runs |
| FORTRAN | `trebago` | numeric | solar angle model | primary | runs |
| Ada | `eve` | limits | UV exposure limits | primary | runs |
| APL | `kron` | arrays | daylight grid | primary | runs |
| MUMPS | `myra` | records | worker profile tree | primary | runs |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |
| Modula-2 | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Simula I): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `uv_ok`: (critical) UV exposure limits (UV index x minutes within the skin type's declared dose) in Ada as Eve + APL as KRON witnessed
- - `sun_angle`: (critical) solar elevation by minute (day of year and latitude, integer sine tables) in FORTRAN as Trebago + APL as KRON witnessed
- - `daylight`: daylight grid (days x hours) in APL as KRON
- - `rhythm`: rhythm plan (light before noon, dim after the declared hour) in Ada as Eve
- - `shift_pay`: shift pay (minutes x rate, minor units, refusing on overflow) in COBOL as Sutra
- - `workers`: worker profile tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the forecast says UV 4 and the sensor 7: UV is BOTH
- the test asserts state 3 and that the warning uses 7 while showing both. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id aubade
matrix 368
name Aubade
needs location
needs sys.read
needs fs.read
needs fs.write
needs screen.time
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

