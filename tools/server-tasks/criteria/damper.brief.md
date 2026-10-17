# damper — the complete brief

| | |
|---|---|
| matrix layer | 174 |
| icon group | L2 |
| wave spec | `wave14/174-damper.md` |

## Purpose

watches heat, smoke and power readings (`sys.info`, `power.status`, `log.tail`, `journal.query`) and coordinates alarms and evacuation contacts (`phone.list`, `call.start` on the person's request, `place.here`). zxvd has no method for home heat and smoke sensor readings, notice post: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `journal`
- `sys.read`
- `notify`
- `peers`
- `location`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | incident log | primary | runs |
| FORTRAN | `trebago` | numeric | spread model | primary | runs |
| Ada | `eve` | limits | temperature limits | primary | runs |
| APL | `kron` | arrays | sensor grid | primary | runs |
| MUMPS | `myra` | records | building tree | primary | runs |
| JOVIAL | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Scala | `-` | dataflow | dataflow | primary | planned: needs Lingua front-end |
| Objective-C | `-` | objects | domain objects | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (JOVIAL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "raise the alarm" (any sensor over its temperature limit OR smoke reported OR a reading that is BOTH) in Ada as Eve + APL as KRON witnessed
- temperature rise per minute over the last readings (tenths of a degree) against the declared rate exact in FORTRAN as Trebago
- temperature limits in Ada as Eve
- sensor grid (room by sensor) in APL as KRON
- building tree (floors, rooms, exits) in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id damper
matrix 174
name Damper
needs journal
needs sys.read
needs notify
needs peers
needs location
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

