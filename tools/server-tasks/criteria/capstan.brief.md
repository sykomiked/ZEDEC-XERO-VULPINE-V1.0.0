# capstan — the complete brief

| | |
|---|---|
| matrix layer | 359 |
| icon group | L3 |
| wave spec | `wave30/359-capstan.md` |

## Purpose

Monitors and dispatches wind and water turbines: readings from this machine (`power.status`, `power.top`), station records (`place.list`), maintenance plans and output in the Hoard (`files.read`, `files.write`), grid payments in the books (`ledger.post`, `ledger.entries`). Turbine telemetry has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `location`
- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | grid payments | primary | runs |
| FORTRAN | `trebago` | numeric | blade aerodynamics | primary | runs |
| Ada | `eve` | limits | rotor speed limits | primary | runs |
| APL | `kron` | arrays | farm output grid | primary | runs |
| MUMPS | `myra` | records | turbine part tree | primary | runs |
| Ladder logic | `-` | control | machine control | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Ladder logic): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `rotor_ok`: (critical) rotor speed limits (rpm at most the rating, a cut-out wind speed) in Ada as Eve + APL as KRON witnessed
- a shutdown is the operator's
- - `grid_pay`: (critical) grid payments (kWh x tariff in minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `aero`: blade aerodynamics (power from wind speed by the turbine's integer power curve) in FORTRAN as Trebago
- - `output`: farm output grid (turbines x hours) in APL as KRON
- - `parts`: turbine part tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two vibration sensors on one turbine disagree: the turbine is BOTH
- the test asserts state 3 and that an inspection is scheduled, not ignored or shut down. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
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
id capstan
matrix 359
name Capstan
needs sys.read
needs location
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

