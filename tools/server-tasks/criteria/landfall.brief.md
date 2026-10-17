# landfall — the complete brief

| | |
|---|---|
| matrix layer | 356 |
| icon group | L3 |
| wave spec | `wave30/356-landfall.md` |

## Purpose

Plans and records expeditions: routes and offline navigation (`place.here`, `place.list`, `place.add`), gear lists in the Hoard (`files.read`, `files.write`), check-ins over the mesh or radio (`chat.post`, `ehop.bands`, `ehop.seal`, `harmonic.encode`), alerts to contacts if a return time is missed (on the person's prior setup).

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`
- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | permit fees | primary | runs |
| FORTRAN | `trebago` | numeric | route energy model | primary | runs |
| Ada | `eve` | limits | weather and turnaround limits | primary | runs |
| APL | `kron` | arrays | waypoint arrays | primary | runs |
| MUMPS | `myra` | records | team roster tree | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `turnaround`: (critical) "turn around now" (past the turnaround time OR a weather limit crossed OR a member's limit reached) in Ada as Eve + APL as KRON witnessed
- - `overdue`: (critical) "raise overdue" (no check-in by the declared return time plus grace) in Ada as Eve + APL as KRON witnessed
- the alert goes only to contacts the person named
- - `energy`: route energy model (ascent and distance to kcal and hours, integer tables) in FORTRAN as Trebago
- - `waypoints`: waypoint arrays in APL as KRON
- - `permits`: permit fees (minor units, refusing on overflow) in COBOL as Sutra
- - `roster`: team roster tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the satellite fix and dead reckoning differ by 900 m: the position is BOTH
- the test asserts state 3 and that an area covering both is shown, not a single point. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id landfall
matrix 356
name Landfall
needs location
needs fs.read
needs fs.write
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

