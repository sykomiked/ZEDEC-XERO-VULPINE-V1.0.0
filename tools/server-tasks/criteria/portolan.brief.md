# portolan — the complete brief

| | |
|---|---|
| matrix layer | 344 |
| icon group | L3 |
| wave spec | `wave29/344-portolan.md` |

## Purpose

Surveys and publishes maps of land, buildings and routes, merging field notes, drone scans and old charts: places (`place.here`, `place.list`, `place.add`), notes and scans in the Hoard (`files.read`, `files.write`, `search.query`), the published map by CID (`aipi.put`, `web4.cid`, `spaces.set_cid`). Drone flight logs and chart image import have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`
- `search`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | parcel registry fees | primary | runs |
| FORTRAN | `trebago` | numeric | projection maths | primary | runs |
| Ada | `eve` | limits | no-fly zone limits | primary | runs |
| APL | `kron` | arrays | elevation grids | primary | runs |
| MUMPS | `myra` | records | place hierarchy | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |
| MATLAB | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| Ladder logic | `-` | control | machine control | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.
- **-** (MATLAB): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `merge_ok`: (critical) "merge this source" (its survey closes within tolerance AND its date is known AND its datum is one of the declared ones) in Ada as Eve + APL as KRON witnessed
- - `project`: (critical) projection from microdegrees to grid millimetres by an integer table (transverse Mercator series in fixed point) in FORTRAN as Trebago + APL as KRON witnessed
- - `no_fly`: no-fly zone limits for drone scans (inside a declared zone is refused, altitude at most the declared metres) in Ada as Eve
- - `elevation`: elevation grids (cells x metres, slope per cell) in APL as KRON
- - `registry_fee`: parcel registry fees (minor units, refusing on overflow) in COBOL as Sutra
- - `places`: place hierarchy (region, parcel, building, route) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): survey A puts a boundary corner at (1000, 2000) mm and survey B at (1000, 2350) for the same parcel, so the strip between is claimed by both: the strip's state is BOTH
- the test asserts state 3 and that the map draws both lines and marks the strip as disputed, not redrawn. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |

## The manifest contract

```
zxapp 1
id portolan
matrix 344
name Portolan
needs location
needs fs.read
needs fs.write
needs search
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

