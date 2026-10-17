# heartstring — the complete brief

| | |
|---|---|
| matrix layer | 357 |
| icon group | L3 |
| wave spec | `wave30/357-heartstring.md` |

## Purpose

Looks after someone you love: shared care plans, medication reminders, mood check-ins and family updates in the Hoard (`files.read`, `files.write`), the family space (`spaces.get`, `spaces.list`), notices (`notes.list`, `notes.mark_read`), alarms from the home (`log.tail`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `peers`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | care costs | primary | runs |
| FORTRAN | `trebago` | numeric | trend smoothing | primary | runs |
| Ada | `eve` | limits | dose and alert limits | primary | runs |
| APL | `kron` | arrays | mood grid | primary | runs |
| MUMPS | `myra` | records | family care tree | primary | runs |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| IPL | `-` | lists | lists | primary | planned: needs Lingua front-end |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (IPL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `alert_ok`: (critical) dose and alert limits (a missed dose or out-of-range reading alerts the named carers, at most n an hour) in Ada as Eve + APL as KRON witnessed
- - `smooth`: (critical) trend smoothing (7-day moving sum of mood and sleep, integers) in FORTRAN as Trebago + APL as KRON witnessed
- - `updates`: family update routing (which carer hears which change) in Ada as Eve
- - `mood`: mood grid (days x carers' reports) in APL as KRON
- - `costs`: care costs (minor units, refusing on overflow) in COBOL as Sutra
- - `care_tree`: family care tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): one carer reports "cheerful" and another "withdrawn" for the same day: the day is BOTH
- the test asserts state 3 and that both reports stay visible to the family. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id heartstring
matrix 357
name Heartstring
needs fs.read
needs fs.write
needs peers
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

