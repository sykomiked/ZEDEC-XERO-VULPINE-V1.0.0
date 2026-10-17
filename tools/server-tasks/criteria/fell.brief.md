# fell — the complete brief

| | |
|---|---|
| matrix layer | 253 |
| icon group | L2 |
| wave spec | `wave21/253-fell.md` |

## Purpose

Coordinates search and rescue teams: maps and places (`place.here`, `place.list`, `place.add`), radio messages (`ehop.bands`, `ehop.seal`, `ehop.open`, `harmonic.encode`, `harmonic.decode`, `freight.pack`), the team room (`chat.post`, `chat.feed`, `pspace.post`), casualty records in the Hoard (`files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `location`
- `peers`
- `net`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | callout records | primary | runs |
| FORTRAN | `trebago` | numeric | search area model | primary | runs |
| Ada | `eve` | limits | crew safety limits | primary | runs |
| APL | `kron` | arrays | sector grid | primary | runs |
| MUMPS | `myra` | records | casualty tree | primary | runs |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| K | `kron` | query | queries | primary | runs |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) sector priority (probability of area x10000 per sector, shifted by searched coverage, summing exactly to 10000) in FORTRAN as Trebago + APL as KRON witnessed
- a suggestion for the incident commander
- - (critical) crew safety (shift at most 12 hours, check-in at most every 60 minutes, overdue raised) in Ada as Eve + APL as KRON witnessed
- - sector grid in APL as KRON, and the board's filters (unsearched sectors, sightings by time) as K queries in APL as KRON
- - callout records (crew hours and mileage claims in minor units, refusing on overflow) in COBOL as Sutra
- - casualty tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a sighting in sector 4 at 14:10 and another in sector 9 at 14:12 give BOTH
- the test asserts state 3 and that neither sector is dropped. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id fell
matrix 253
name Fell
needs location
needs peers
needs net
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

