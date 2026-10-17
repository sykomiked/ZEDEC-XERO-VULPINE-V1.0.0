# shipwright — the complete brief

| | |
|---|---|
| matrix layer | 190 |
| icon group | L2 |
| wave spec | `wave15/190-shipwright.md` |

## Purpose

forecasts tides, floods and surges for districts (`place.here`, `place.list`) and sends alerts to people in harm's way (`pspace.post`) once a person approves, with history from `journal.query`. zxvd has no method for gauge and tide readings: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `journal`
- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | incident accounts | primary | runs |
| FORTRAN | `trebago` | numeric | flood model | primary | runs |
| Ada | `eve` | limits | alert thresholds | primary | runs |
| APL | `kron` | arrays | gauge grid | primary | runs |
| MUMPS | `myra` | records | district tree | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- water level ahead (gauge trend plus rainfall in mm x the catchment factor) in FORTRAN as Trebago + APL as KRON witnessed
- "warn this district" (level at or above the threshold, or a gauge reading BOTH) in Ada as Eve + APL as KRON witnessed
- alert thresholds in Ada as Eve
- gauge grid in APL as KRON
- district tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## The manifest contract

```
zxapp 1
id shipwright
matrix 190
name Shipwright
needs location
needs journal
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

