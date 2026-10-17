# piccolo — the complete brief

| | |
|---|---|
| matrix layer | 385 |
| icon group | L3 |
| wave spec | `wave32/385-piccolo.md` |

## Purpose

Plans pollinator-friendly gardens and tracks bees, birds and blooms with neighbours: beds and sightings in the Hoard (`files.read`, `files.write`, `search.query`), places (`place.here`, `place.list`), the neighbours' circle (`social.post`, `social.feed`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `location`
- `peers`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | seed swap credits | primary | runs |
| FORTRAN | `trebago` | numeric | bloom timing | primary | runs |
| Ada | `eve` | limits | pesticide limits | primary | runs |
| APL | `kron` | arrays | bloom calendar grid | primary | runs |
| MUMPS | `myra` | records | species tree | primary | runs |
| APT | `-` | toolpath | tool paths | primary | planned: needs Lingua front-end |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Modula-2 | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (APT): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `spray_ok`: (critical) pesticide limits (none while a declared pollinator plant blooms
- label rate otherwise) in Ada as Eve + APL as KRON witnessed
- - `bloom`: (critical) bloom timing (degree days to flowering by species table) in FORTRAN as Trebago + APL as KRON witnessed
- - `planting`: planting layout across beds (bloom gaps filled month by month) in APL as KRON
- - `calendar`: bloom calendar grid in APL as KRON
- - `swaps`: seed swap credits (refusing on overflow) in COBOL as Sutra
- - `species`: species tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two neighbours report a rare bee and a common one for the same photo: the sighting is BOTH
- the test asserts state 3 and that both stay until verified. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id piccolo
matrix 385
name Piccolo
needs fs.read
needs fs.write
needs search
needs location
needs peers
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

