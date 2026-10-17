# gust — the complete brief

| | |
|---|---|
| matrix layer | 218 |
| icon group | L2 |
| wave spec | `wave18/218-gust.md` |

## Purpose

Plans children's outdoor play: where the child is and the family's safe areas (`place.here`, `place.list`, location shared only after `place.ask`), time left from the child's limits (`screen.check`, `screen.status`), the family's seats (`seats.list`); plans and the activity log in the Hoard (`files.read`, `files.write`). There is no weather source (as Weatherglass): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `screen.time`
- `sys.read`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | activity log | primary | runs |
| FORTRAN | `trebago` | numeric | wind and UV model | primary | runs |
| Ada | `eve` | limits | time and distance limits | primary | runs |
| APL | `kron` | arrays | forecast grid | primary | runs |
| MUMPS | `myra` | records | family tree | primary | runs |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Pascal | `pascal` | stepwise | step-by-step teaching | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Simula I): see docs/LANGUAGE_CATALOGUE.md for this tier
- **pascal** (Pascal): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "may play outside now" = no warning from Weatherglass's block AND within the time and distance limits AND inside a safe area, in Ada as Eve + APL as KRON witnessed
- - wind and UV model (UV index x minutes outside as an exposure total
- gust above the limit by hour) exact in FORTRAN as Trebago, witnessed by APL as KRON where it changes the answer above
- - time and distance limits (minutes 0..480, metres from home 0..5000, per child) in Ada as Eve
- - forecast grid (hour x wind, UV, temperature
- the play windows) in APL as KRON
- - activity log as fixed records (child, outings, minutes), totals refusing on overflow, in COBOL as Sutra
- - family tree (child, guardians, who may approve) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): forecast A wind 25 km/h (under a 40 limit, TRUE) and forecast B 55 km/h (FALSE) give BOTH
- both forecasts are shown to the parents, the app neither allows nor blocks, and the test asserts state 3 and that the parent's answer decides. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id gust
matrix 218
name Gust
needs location
needs screen.time
needs sys.read
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

