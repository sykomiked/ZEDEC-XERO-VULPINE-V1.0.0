# reedbed — the complete brief

| | |
|---|---|
| matrix layer | 257 |
| icon group | L2 |
| wave spec | `wave21/257-reedbed.md` |

## Purpose

Monitors wetlands and water quality with low-power sensor nodes: sites (`place.list`), records in the Hoard (`files.read`, `files.write`, `search.query`), the dataset published locally (`aipi.put`, `web4.cid`), the link (`sys.net`). Sensor input has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `net`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | permit records | primary | runs |
| FORTRAN | `trebago` | numeric | water chemistry model | primary | runs |
| Ada | `eve` | limits | alert thresholds | primary | runs |
| APL | `kron` | arrays | sensor grid | primary | runs |
| MUMPS | `myra` | records | site tree | primary | runs |
| JOVIAL | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Scala | `-` | dataflow | dataflow | primary | planned: needs Lingua front-end |
| Forth | `forth` | stack | small interpreters | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (JOVIAL): see docs/LANGUAGE_CATALOGUE.md for this tier
- **forth** (Forth): the run tier: stack words. '<entry> ( args -- result )' with integer words. Range laws are natural here.

## The spec's Logic clauses (what each component computes)

- - (critical) alert thresholds (pH x100 within 650..850, dissolved oxygen at least 5000 ug/L, the owner's other bounds) in Ada as Eve + APL as KRON witnessed
- - (critical) the node's raw count to a reading with its range check in Forth (twin Cairndrake) (on the node) + FORTRAN as Trebago witnessed
- - water chemistry (oxygen saturation x100 from a temperature table, temperature-corrected conductivity) exact in FORTRAN as Trebago
- - sensor grid (sensors x hours) in APL as KRON
- - permit records (sample counts per permit period, refusing on overflow) in COBOL as Sutra
- - site tree in MUMPS as MYRA
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): two drifting sensors in one pool read pH 7.2 and 8.9, giving BOTH
- the test asserts state 3, a calibration flag, and both readings kept. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| `within_range` | truth | `value min max` | eve | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id reedbed
matrix 257
name Reedbed
needs fs.read
needs fs.write
needs search
needs net
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

