# bourn — the complete brief

| | |
|---|---|
| matrix layer | 249 |
| icon group | L2 |
| wave spec | `wave20/249-bourn.md` |

## Purpose

Logs brewing batches: recipes, temperatures, gravity, ageing and duty, kept in the Hoard (`files.read`, `files.write`), the books for duty (`ledger.entries`), the jurisdiction (`jurisdiction.get`). Sensor input has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ledger.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | excise records | primary | runs |
| FORTRAN | `trebago` | numeric | fermentation model | primary | runs |
| Ada | `eve` | limits | temperature limits | primary | runs |
| APL | `kron` | arrays | batch grid | primary | runs |
| MUMPS | `myra` | records | recipe and lot tree | primary | runs |
| CPL | `-` | formula | formulas | primary | planned: needs Lingua front-end |
| JCL | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (CPL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) strength x100 from gravities x1000 ((OG - FG) x 13125 / 100, floor) in FORTRAN as Trebago + APL as KRON witnessed
- - (critical) duty due (litres x strength x rate, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - temperature limits (the yeast's range in tenths of a degree) in Ada as Eve
- - batch grid (batches x days x readings) in APL as KRON
- - recipe and lot tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the hydrometer reads 1012 and the sensor 1020, giving BOTH
- the test asserts state 3 and that the batch is held for a check, not marked finished. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id bourn
matrix 249
name Bourn
needs fs.read
needs fs.write
needs ledger.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

