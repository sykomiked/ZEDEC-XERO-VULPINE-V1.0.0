# athanor — the complete brief

| | |
|---|---|
| matrix layer | 351 |
| icon group | L3 |
| wave spec | `wave29/351-athanor.md` |

## Purpose

Plans and runs chemistry or brewing processes: recipes, batch records and hazard checks in the Hoard (`files.read`, `files.write`, `search.query`), heat and power readings (`sys.info`, `power.status`), alarms (`log.tail`, `journal.query`). Process sensors have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `sys.read`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | batch costing | primary | runs |
| FORTRAN | `trebago` | numeric | reaction kinetics | primary | runs |
| Ada | `eve` | limits | pressure and heat limits | primary | runs |
| APL | `kron` | arrays | concentration arrays | primary | runs |
| MUMPS | `myra` | records | batch lineage tree | primary | runs |
| JOSS | `-` | formula | formulas | primary | planned: needs Lingua front-end |
| JCL | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (JOSS): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `vessel_ok`: (critical) pressure and heat limits (kPa and tenths of a degree within the vessel's rating, rise per minute within the declared rate) in Ada as Eve + APL as KRON witnessed
- - `batch_cost`: (critical) batch costing (reagents and energy in minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `kinetics`: reaction kinetics (first-order conversion by step from an integer rate table) in FORTRAN as Trebago
- - `schedule`: run schedule (step start times as a running sum) in FORTRAN as Trebago
- - `conc`: concentration arrays (species x step) in APL as KRON
- - `lineage`: batch lineage tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the lab result passes a batch and the in-line sensor fails it: the batch is BOTH
- the test asserts state 3 and that it is quarantined, neither shipped nor discarded. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id athanor
matrix 351
name Athanor
needs fs.read
needs fs.write
needs search
needs sys.read
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

