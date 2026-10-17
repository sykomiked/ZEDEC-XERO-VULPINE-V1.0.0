# binnacle — the complete brief

| | |
|---|---|
| matrix layer | 147 |
| icon group | L2 |
| wave spec | `wave12/147-binnacle.md` |

## Purpose

position, heading and routes worked out offline from star sights, the clock (`sys.info`) and saved places (`place.here`, `place.list`, `place.add`), with no outside service. zxvd has no method for star-sight entry from an instrument, chart data: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | voyage log | primary | runs |
| FORTRAN | `trebago` | numeric | fix calculation | primary | runs |
| Ada | `eve` | limits | safe-heading bounds | primary | runs |
| APL | `kron` | arrays | bearing arrays | primary | runs |
| MUMPS | `myra` | records | waypoint tree | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- a fix from two lines of position (2x2 integer system by Cramer's rule, microdegrees, the division reduced with kernel/src/rational in C) in FORTRAN as Trebago + APL as KRON witnessed
- bearing and distance to every waypoint (fixed point, over the waypoint vectors) in APL as KRON
- "safe heading" (0..35999 centidegrees, outside every marked hazard sector, cross-track within the declared limit) in Ada as Eve + APL as KRON witnessed
- distance run per leg in metres, refused on overflow, in COBOL as Sutra + FORTRAN as Trebago witnessed
- waypoint tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id binnacle
matrix 147
name Binnacle
needs location
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

