# solarium — the complete brief

| | |
|---|---|
| matrix layer | 292 |
| icon group | L3 |
| wave spec | `wave24/292-solarium.md` |

## Purpose

looks after a garden or greenhouse: planting plans, watering, soil and light sensors, and harvest logs (`files.read`, `files.write`, `place.here`). zxvd has no method for soil and light sensor readings, valve control: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `location`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | seed and produce accounts | primary | runs |
| FORTRAN | `trebago` | numeric | evapotranspiration model | primary | runs |
| Ada | `eve` | limits | watering limits | primary | runs |
| APL | `kron` | arrays | bed sensor grid | primary | runs |
| MUMPS | `myra` | records | plant lineage | primary | runs |
| IEC 61131-3 Structured Text | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Modula-2 | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (IEC 61131-3 Structured Text): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `water_ok` (Dibber's "may water now" AND soil below the declared moisture AND no rain in the forecast window) in Ada as Eve + APL as KRON witnessed
- `evapotranspiration` (daily water loss from temperature and light, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `produce_accounts` (seeds and produce in minor units) in COBOL as Sutra
- `beds` sensor grid in APL as KRON
- `lineage` (plant lineage) in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id solarium
matrix 292
name Solarium
needs fs.read
needs fs.write
needs location
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

