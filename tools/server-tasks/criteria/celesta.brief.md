# celesta — the complete brief

| | |
|---|---|
| matrix layer | 406 |
| icon group | L3 |
| wave spec | `wave34/406-celesta.md` |

## Purpose

Plans mountain trips and winter sports with snow reports, avalanche risk, lift passes and group check-ins: places (`place.here`, `place.list`), check-ins (`chat.post`, `pspace.post`), plans in the Hoard (`files.read`, `files.write`), lift pass payments (`ledger.post`). Snow and avalanche feeds have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `peers`
- `net`
- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | lift pass sales | primary | runs |
| FORTRAN | `trebago` | numeric | snowpack model | primary | runs |
| Ada | `eve` | limits | slope angle limits | primary | runs |
| APL | `kron` | arrays | terrain grid | primary | runs |
| MUMPS | `myra` | records | party tree | primary | runs |
| PHP | `php` | report | reports | primary | planned: SDK host only, no in-app runtime yet |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Modula-2 | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **php** (PHP): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Simula 67): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `slope_ok`: (critical) slope angle limits (no route over the declared degrees at the day's danger level) in Ada as Eve + APL as KRON witnessed
- - `snowpack`: (critical) snowpack model (load from new snow and wind, integer tables) in FORTRAN as Trebago + APL as KRON witnessed
- - `report`: trip report counts (members, check-ins, alerts) in APL as KRON
- - `terrain`: terrain grid in APL as KRON
- - `passes`: lift pass sales (minor units, refusing on overflow) in COBOL as Sutra
- - `party`: party tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two sources rate the avalanche danger 2 and 4: the rating is BOTH
- the test asserts state 3 and that routes use 4 while showing why the sources differ. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id celesta
matrix 406
name Celesta
needs location
needs peers
needs net
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

