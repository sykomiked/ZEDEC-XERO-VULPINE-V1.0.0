# glebe — the complete brief

| | |
|---|---|
| matrix layer | 255 |
| icon group | L2 |
| wave spec | `wave21/255-glebe.md` |

## Purpose

Plans harvests: fields and the place (`place.here`, `place.list`), crop sales (`net.offers`, `net.command` sell, `ledger.entries`), plans in the Hoard (`files.read`, `files.write`), farm energy (`power.status`). There is no weather source (as Weatherglass): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `peers`
- `net`
- `ledger.read`
- `fs.read`
- `fs.write`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | crop sales | primary | runs |
| FORTRAN | `trebago` | numeric | yield model | primary | runs |
| Ada | `eve` | limits | storage limits | primary | runs |
| APL | `kron` | arrays | field grid | primary | runs |
| MUMPS | `myra` | records | farm and field tree | primary | runs |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Modula-2 | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Simula 67): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "fits in storage" (tonnes and moisture within each store's capacity and limit) in Ada as Eve + APL as KRON witnessed
- - (critical) crop sale value (kg x price, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - yield model (area m2 x yield g/m2, labour hours per hectare) exact in FORTRAN as Trebago
- - field grid (fields x days, dry-day runs for weather windows) in APL as KRON
- - farm and field tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): yield estimate A 12 t and estimate B 18 t give BOTH
- the test asserts state 3 and that storage is planned for 18 t and sales for 12 t (both cases). Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id glebe
matrix 255
name Glebe
needs location
needs peers
needs net
needs ledger.read
needs fs.read
needs fs.write
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

