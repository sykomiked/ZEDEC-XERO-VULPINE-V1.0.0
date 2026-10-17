# hydrophone — the complete brief

| | |
|---|---|
| matrix layer | 379 |
| icon group | L3 |
| wave spec | `wave31/379-hydrophone.md` |

## Purpose

Tracks marine life and ship noise with underwater listening stations, sharing sightings and protecting migration routes: stations (`net.peers`, `devmesh.devices`), messages relayed (`ehop.seal`, `ehop.open`, `harmonic.encode`), places (`place.list`), records (`files.read`, `files.write`, `search.query`). Hydrophone audio has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `location`
- `fs.read`
- `fs.write`
- `search`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | shipping levy | primary | runs |
| FORTRAN | `trebago` | numeric | acoustic signal processing | primary | runs |
| Ada | `eve` | limits | zone speed limits | primary | runs |
| APL | `kron` | arrays | spectrogram arrays | primary | runs |
| MUMPS | `myra` | records | species tree | primary | runs |
| IPL | `-` | lists | lists | primary | planned: needs Lingua front-end |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |
| Racket | `-` | parse | parsing | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (IPL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `zone_ok`: (critical) zone speed limits (a vessel's speed in a protective zone at most the declared knots while the zone is active) in Ada as Eve + APL as KRON witnessed
- - `detect`: (critical) acoustic detection (band energy over the noise floor in integer spectra) in FORTRAN as Trebago + APL as KRON witnessed
- - `spectro`: spectrogram arrays in APL as KRON
- - `rules`: zone activation rules (a confirmed or BOTH sighting keeps the zone active) in Ada as Eve
- - `levy`: shipping levy (minor units, refusing on overflow) in COBOL as Sutra
- - `species`: species tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the acoustic station detects a whale and the visual watch reports none: the sighting is BOTH
- the test asserts state 3 and that the protective zone stays active until confirmed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id hydrophone
matrix 379
name Hydrophone
needs peers
needs net
needs location
needs fs.read
needs fs.write
needs search
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

