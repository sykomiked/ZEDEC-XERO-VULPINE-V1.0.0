# wardian — the complete brief

| | |
|---|---|
| matrix layer | 405 |
| icon group | L3 |
| wave spec | `wave34/405-wardian.md` |

## Purpose

Runs greenhouses, vertical farms or terrariums: climate, water, light, plant health and harvests in the Hoard (`files.read`, `files.write`), stations (`place.list`, `devmesh.devices`), produce sales (`ledger.post`, `ledger.entries`). Climate sensors and actuators have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `location`
- `peers`
- `net`
- `ledger.read`
- `ledger.post`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | produce sales | primary | runs |
| FORTRAN | `trebago` | numeric | climate model | primary | runs |
| Ada | `eve` | limits | humidity and CO2 limits | primary | runs |
| APL | `kron` | arrays | sensor grid | primary | runs |
| MUMPS | `myra` | records | bed and plant tree | primary | runs |
| Ladder logic | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Delphi | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Ladder logic): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `climate_ok`: (critical) humidity and CO2 limits (per cent x10 and ppm within each crop's band
- a step change proposed, never forced) in Ada as Eve + APL as KRON witnessed
- - `climate`: (critical) climate model (vapour pressure deficit from temperature and humidity tables) in FORTRAN as Trebago + APL as KRON witnessed
- - `growth_sim`: harvest date estimate from degree days in FORTRAN as Trebago
- - `sensors`: sensor grid in APL as KRON
- - `sales`: produce sales (minor units, refusing on overflow) in COBOL as Sutra
- - `beds`: bed and plant tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two humidity sensors read 62% and 81%: the zone is BOTH
- the test asserts state 3 and that control steers cautiously (no large change) while flagging both. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id wardian
matrix 405
name Wardian
needs fs.read
needs fs.write
needs location
needs peers
needs net
needs ledger.read
needs ledger.post
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

