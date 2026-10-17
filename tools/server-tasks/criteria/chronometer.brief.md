# chronometer — the complete brief

| | |
|---|---|
| matrix layer | 404 |
| icon group | L3 |
| wave spec | `wave34/404-chronometer.md` |

## Purpose

Coordinates shifts, bookings and deadlines across time zones, with exact time from trusted clocks: the clock (`sys.info`, `devmesh.devices`, `net.peers`), rotas and bookings in the Hoard (`files.read`, `files.write`), records proven by the journal (`journal.query`). A peer clock-offset query has no method (as Escapement): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `peers`
- `fs.read`
- `fs.write`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | billable hours | primary | runs |
| FORTRAN | `trebago` | numeric | clock drift correction | primary | runs |
| Ada | `eve` | limits | rest limits | primary | runs |
| APL | `kron` | arrays | rota grid | primary | runs |
| MUMPS | `myra` | records | team tree | primary | runs |
| Haskell | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Haskell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `rest_ok`: (critical) rest limits (at least the declared hours between shifts, at most the weekly hours) in Ada as Eve + APL as KRON witnessed
- - `drift`: (critical) clock drift correction (offset trend per hour from paired readings) in FORTRAN as Trebago + APL as KRON witnessed
- - `parallel`: booking conflicts across people and rooms in APL as KRON
- - `rota`: rota grid in APL as KRON
- - `billable`: billable hours (minutes x rate, minor units, refusing on overflow) in COBOL as Sutra
- - `team`: team tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two trusted clocks differ by 900 ms for one event: the timestamp is BOTH
- the test asserts state 3 and that it is held with a window covering both. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id chronometer
matrix 404
name Chronometer
needs sys.read
needs peers
needs fs.read
needs fs.write
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

