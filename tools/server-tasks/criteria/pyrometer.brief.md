# pyrometer — the complete brief

| | |
|---|---|
| matrix layer | 361 |
| icon group | L3 |
| wave spec | `wave30/361-pyrometer.md` |

## Purpose

Tracks home and building heating, fuel use and fire risk: readings (`sys.info`, `power.status`, `log.tail`, `journal.query`), inspections and bills in the Hoard (`files.read`, `files.write`), contacts rung on the person's request (`call.start`). Boiler, thermostat and smoke-alarm devices have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `journal`
- `fs.read`
- `fs.write`
- `peers`
- `location`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | fuel bills | primary | runs |
| FORTRAN | `trebago` | numeric | heat transfer model | primary | runs |
| Ada | `eve` | limits | temperature cutoffs | primary | runs |
| APL | `kron` | arrays | room heat grid | primary | runs |
| MUMPS | `myra` | records | building tree | primary | runs |
| Bourne shell | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Bourne shell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `cutoff`: (critical) temperature cutoffs (any room over its limit, or a rise faster than the declared rate, asks for a shutoff the person confirms) in Ada as Eve + APL as KRON witnessed
- - `fuel_bill`: (critical) fuel bills (units x tariff bands, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `heat`: heat transfer model (room heat loss W from area x U value x temperature difference) in FORTRAN as Trebago
- - `inspect_due`: inspection due (days since each inspection against its interval) in APL as KRON
- - `rooms`: room heat grid in APL as KRON
- - `building`: building tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the smoke alarm sounds and the thermometer shows normal: the room is BOTH
- the test asserts state 3 and that a check is raised, the alarm never ignored. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id pyrometer
matrix 361
name Pyrometer
needs sys.read
needs journal
needs fs.read
needs fs.write
needs peers
needs location
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

