# verderer — the complete brief

| | |
|---|---|
| matrix layer | 176 |
| icon group | L2 |
| wave spec | `wave14/176-verderer.md` |

## Purpose

logs wildlife sightings, trails and habitats with photos (`files.read`, `files.write`) and maps (`place.here`, `place.add`, `place.list`), searchable with `search.query`.

## Capabilities (the exact registry names for `needs`)

- `camera`
- `location`
- `fs.read`
- `fs.write`
- `search`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | survey records | primary | runs |
| FORTRAN | `trebago` | numeric | population model | primary | runs |
| Ada | `eve` | limits | protected-area limits | primary | runs |
| APL | `kron` | arrays | sighting grid | primary | runs |
| MUMPS | `myra` | records | species tree | primary | runs |
| MATLAB | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (MATLAB): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- population estimate (marked x caught / recaptured, floor, refused when none recaptured) in FORTRAN as Trebago + APL as KRON witnessed
- "share this place" (a protected species' place is coarsened to the 10 km grid before it leaves the device) in Ada as Eve + APL as KRON witnessed
- protected-area limits in Ada as Eve
- sighting grid (species by grid cell) in APL as KRON
- species tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |

## The manifest contract

```
zxapp 1
id verderer
matrix 176
name Verderer
needs camera
needs location
needs fs.read
needs fs.write
needs search
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

