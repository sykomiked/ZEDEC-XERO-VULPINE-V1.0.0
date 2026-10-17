# ephemeris — the complete brief

| | |
|---|---|
| matrix layer | 299 |
| icon group | L3 |
| wave spec | `wave25/299-ephemeris.md` |

## Purpose

plans observing nights: object positions, rise and set times, weather and a log of what you saw (`place.here`, `sys.info`, `net.peers`, `files.write`). zxvd has no method for weather source: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `sys.read`
- `camera`
- `fs.write`
- `peers`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | equipment ledger | primary | runs |
| FORTRAN | `trebago` | numeric | orbital mechanics | primary | runs |
| Ada | `eve` | limits | exposure limits | primary | runs |
| APL | `kron` | arrays | sky coordinate arrays | primary | runs |
| MUMPS | `myra` | records | observation catalogue | primary | runs |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |
| Oberon | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Simula I): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- `exposure_ok` (exposure and session length inside the declared limits) in Ada as Eve + APL as KRON witnessed
- `orbit` (rise, transit and set in minutes of day) in FORTRAN as Trebago + APL as KRON witnessed
- `equipment` ledger (items in minor units) in COBOL as Sutra
- `sky` coordinate arrays in APL as KRON
- `observations` catalogue in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id ephemeris
matrix 299
name Ephemeris
needs location
needs sys.read
needs camera
needs fs.write
needs peers
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

