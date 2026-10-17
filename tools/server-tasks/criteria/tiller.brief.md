# tiller — the complete brief

| | |
|---|---|
| matrix layer | 175 |
| icon group | L2 |
| wave spec | `wave14/175-tiller.md` |

## Purpose

one control panel for vehicles, drones and machines with hard safety limits, reading link rates (`sys.net`) and places (`place.here`, `place.list`). zxvd has no method for any vehicle, drone or machine telemetry and control method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `sys.read`
- `net`
- `camera`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | trip log | primary | runs |
| FORTRAN | `trebago` | numeric | flight dynamics | primary | runs |
| Ada | `eve` | limits | control envelope | primary | runs |
| APL | `kron` | arrays | telemetry arrays | primary | runs |
| MUMPS | `myra` | records | craft tree | primary | runs |
| Ladder logic | `-` | control | machine control | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Ladder logic): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "command inside the envelope" (speed, altitude, bank and distance from home at most the declared limits) in Ada as Eve + APL as KRON witnessed
- "hold safe state" (any input BOTH, or the link rate stale beyond the declared ms) in Ada as Eve + APL as KRON witnessed
- stopping distance from speed and deceleration exact in FORTRAN as Trebago
- telemetry arrays in APL as KRON
- craft tree and trip log in MUMPS as MYRA

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
id tiller
matrix 175
name Tiller
needs location
needs sys.read
needs net
needs camera
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

