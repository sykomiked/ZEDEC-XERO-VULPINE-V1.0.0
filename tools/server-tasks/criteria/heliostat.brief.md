# heliostat — the complete brief

| | |
|---|---|
| matrix layer | 402 |
| icon group | L3 |
| wave spec | `wave33/402-heliostat.md` |

## Purpose

Plans, monitors and shares solar power across homes, settling surplus energy credits between neighbours: power (`power.status`, `power.top`, `power.budget`), the books (`ledger.post`, `ledger.entries`, `ledger.hold`, `ledger.holds`, `ledger.release`), votes on shared rules (`spaces.propose`, `spaces.vote`). Inverter and meter readings have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `ledger.read`
- `ledger.post`
- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | energy credits | primary | runs |
| FORTRAN | `trebago` | numeric | irradiance model | primary | runs |
| Ada | `eve` | limits | inverter limits | primary | runs |
| APL | `kron` | arrays | panel output grid | primary | runs |
| MUMPS | `myra` | records | household tree | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Bash | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| Tcl | `-` | scripting | scripting | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `inverter_ok`: (critical) inverter limits (export at most the allowance, battery at least the reserve) in Ada as Eve + APL as KRON witnessed
- - `credits`: (critical) energy credits (surplus Wh x price per neighbour, largest remainder, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- settled only on the owners' approval
- - `irradiance`: irradiance model (Wh per panel from sun minutes and tilt table) in FORTRAN as Trebago
- - `settle_jobs`: settlement schedule (end of each declared period) in Ada as Eve
- - `panels`: panel output grid in APL as KRON
- - `households`: household tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the meter says 12.4 kWh exported and the inverter 10.9: the export is BOTH
- the test (scratch books) asserts state 3 and the credits on `ledger.hold` until checked. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `largest_remainder` | num | `pool w1 w2 w3 w4` | sutra | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id heliostat
matrix 402
name Heliostat
needs sys.read
needs ledger.read
needs ledger.post
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

