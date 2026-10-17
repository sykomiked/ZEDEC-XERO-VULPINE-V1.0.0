# armillary — the complete brief

| | |
|---|---|
| matrix layer | 349 |
| icon group | L3 |
| wave spec | `wave29/349-armillary.md` |

## Purpose

Calibrates and maintains precision machines: readings from this machine (`sys.info`, `sys.disk`, `power.status`), service logs and plans in the Hoard (`files.read`, `files.write`), service times from the shared clock. Readings from other machines and their instruments have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `fs.read`
- `fs.write`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | service contracts | primary | runs |
| FORTRAN | `trebago` | numeric | calibration curves | primary | runs |
| Ada | `eve` | limits | torque and drift limits | primary | runs |
| APL | `kron` | arrays | sensor matrices | primary | runs |
| MUMPS | `myra` | records | assembly tree | primary | runs |
| IEC 61131-3 Structured Text | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Scala | `-` | dataflow | dataflow | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (IEC 61131-3 Structured Text): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `drift_ok`: (critical) torque and drift limits (torque at most the declared N.mm, drift per 1000 hours at most the bound) in Ada as Eve + APL as KRON witnessed
- - `calibrate`: (critical) calibration curve (two-point and multi-point integer correction, reading to true value) in FORTRAN as Trebago + APL as KRON witnessed
- - `sensors`: sensor matrices (sensors x axes, alignment offsets) in APL as KRON
- - `contracts`: service contracts (visits used, cost in minor units, refusing on overflow) in COBOL as Sutra
- - `assembly`: assembly tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two sensors on one axis read 12.004 mm and 12.019 mm with a 5 um tolerance: the axis is BOTH
- the test asserts state 3 and that a recalibration is scheduled, neither sensor trusted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id armillary
matrix 349
name Armillary
needs sys.read
needs fs.read
needs fs.write
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

