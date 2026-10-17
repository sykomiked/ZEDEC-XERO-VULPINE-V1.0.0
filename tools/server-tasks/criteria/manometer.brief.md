# manometer — the complete brief

| | |
|---|---|
| matrix layer | 395 |
| icon group | L3 |
| wave spec | `wave33/395-manometer.md` |

## Purpose

Supervises a power plant or microgrid: core readings, interlocks, operator logs and regulator reports, from this machine (`sys.info`, `power.status`, `power.top`, `log.tail`, `journal.query`), the perimeter (`features.get`), logs and reports in the Hoard (`files.read`, `files.write`). Plant telemetry and interlock control have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `journal`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | generation billing | primary | runs |
| FORTRAN | `trebago` | numeric | thermal hydraulics | primary | runs |
| Ada | `eve` | limits | interlock limits | primary | runs |
| APL | `kron` | arrays | sensor grid | primary | runs |
| MUMPS | `myra` | records | plant component tree | primary | runs |
| IEC 61131-3 Structured Text | `-` | control | machine control | primary | planned: needs Lingua front-end |
| SAS | `-` | report | reports | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (IEC 61131-3 Structured Text): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `interlock`: (critical) interlock limits (any redundant channel out of range, or any BOTH, calls a safe hold for the operator) in Ada as Eve + APL as KRON + FORTRAN as Trebago witnessed (three witnesses)
- - `thermal`: (critical) thermal hydraulics (heat balance, flow x temperature rise, integers) in FORTRAN as Trebago + APL as KRON witnessed
- - `sensors`: sensor grid in APL as KRON
- - `report`: regulator report counts (events per kind per period) in APL as KRON
- - `billing`: generation billing (MWh x tariff, minor units, refusing on overflow) in COBOL as Sutra
- - `plant`: plant component tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two redundant pressure channels read 15.2 and 16.9 MPa: the channel pair is BOTH
- the test asserts state 3 and that a safe hold is proposed, neither channel trusted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id manometer
matrix 395
name Manometer
needs sys.read
needs journal
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

