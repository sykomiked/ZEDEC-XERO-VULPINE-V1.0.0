# lampwright — the complete brief

| | |
|---|---|
| matrix layer | 205 |
| icon group | L2 |
| wave spec | `wave17/205-lampwright.md` |

## Purpose

sets ambient lighting and display scenes across rooms and screens with colour care, reading `ui.get`, `sys.info` and `power.status`. zxvd has no method for lighting device control, display colour set: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `net`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | energy use | primary | runs |
| FORTRAN | `trebago` | numeric | colour math | primary | runs |
| Ada | `eve` | limits | brightness limits | primary | runs |
| APL | `kron` | arrays | light grid | primary | runs |
| MUMPS | `myra` | records | room tree | primary | runs |
| Dart | `dart` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |
| Ladder logic | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Smalltalk | `-` | objects | domain objects | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **dart** (Dart): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Ladder logic): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- colour temperature to RGB (table by kelvin, interpolated) in APL as KRON + FORTRAN as Trebago witnessed
- "within brightness limits" (at most the room's cap, the night cap after the declared hour, flash guard) in Ada as Eve + APL as KRON witnessed
- energy use (Wh x tariff in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- light grid in APL as KRON
- room tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `within_range` | truth | `value min max` | eve | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |

## The manifest contract

```
zxapp 1
id lampwright
matrix 205
name Lampwright
needs sys.read
needs net
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

