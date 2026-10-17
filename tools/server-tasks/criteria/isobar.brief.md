# isobar — the complete brief

| | |
|---|---|
| matrix layer | 239 |
| icon group | L2 |
| wave spec | `wave19/239-isobar.md` |

## Purpose

Runs a local weather station network: stations and sites (`place.here`, `place.list`), station nodes (`devmesh.devices`, `net.peers`), records in the Hoard (`files.read`, `files.write`). Sensor input has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `peers`
- `net`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | observation records | primary | runs |
| FORTRAN | `trebago` | numeric | forecast model | primary | runs |
| Ada | `eve` | limits | sensor range limits | primary | runs |
| APL | `kron` | arrays | station grid | primary | runs |
| MUMPS | `myra` | records | site tree | primary | runs |
| IEC 61131-3 Structured Text | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Elixir | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Clojure | `-` | dataflow | dataflow | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (IEC 61131-3 Structured Text): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "accept this reading" (within the sensor's range and the step limit since the last) in Ada as Eve + APL as KRON witnessed
- - (critical) short forecast from the 3-hour pressure tendency (tenths of a hPa) by rule table in FORTRAN as Trebago + APL as KRON witnessed
- - station grid (stations x hours) in APL as KRON
- - observation records and the site tree in MUMPS as MYRA
- - Sutra is left out: no money or counted business records
- observations are MYRA records
- Paraconsistent (BOTH test, in test_zxapp.c): two stations 400 m apart read 2.1 C and 7.4 C, both within range, giving BOTH
- the test asserts state 3 and that both are kept (a microclimate), not averaged. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `within_range` | truth | `value min max` | eve | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id isobar
matrix 239
name Isobar
needs location
needs peers
needs net
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

