# azimuth — the complete brief

| | |
|---|---|
| matrix layer | 306 |
| icon group | L3 |
| wave spec | `wave25/306-azimuth.md` |

## Purpose

indoor and outdoor wayfinding with step-by-step directions, accessible routes and offline maps (`place.here`, `place.list`, `place.add`, `files.read`). zxvd has no method for beacon readings, indoor maps: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | venue licences | primary | runs |
| FORTRAN | `trebago` | numeric | positioning maths | primary | runs |
| Ada | `eve` | limits | accessibility limits | primary | runs |
| APL | `kron` | arrays | floor grid | primary | runs |
| MUMPS | `myra` | records | building and floor tree | primary | runs |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- `accessible_ok` (a route has no step, gradient over the declared limit or door narrower than the declared width when the person asks for an accessible route) in Ada as Eve + APL as KRON witnessed
- `position` (position from beacon ranges, integer least squares) in FORTRAN as Trebago + APL as KRON witnessed
- `venue_licence` (venue licences in minor units) in COBOL as Sutra
- `floors` grid in APL as KRON
- `buildings` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id azimuth
matrix 306
name Azimuth
needs location
needs fs.read
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

