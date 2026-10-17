# seine — the complete brief

| | |
|---|---|
| matrix layer | 382 |
| icon group | L3 |
| wave spec | `wave32/382-seine.md` |

## Purpose

Spreads a mesh of low-power sensors across water or land, gathering readings that survive lost nodes: pairing (`devmesh.invite`, `devmesh.join`, `devmesh.confirm_sas`), relays (`ehop.bands`, `ehop.seal`, `ehop.open`), peers (`net.peers`), readings in the Hoard (`files.read`, `files.write`). Sensor readings have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `location`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | data sharing credits | primary | runs |
| FORTRAN | `trebago` | numeric | signal filtering | primary | runs |
| Ada | `eve` | limits | battery limits | primary | runs |
| APL | `kron` | arrays | reading arrays | primary | runs |
| MUMPS | `myra` | records | node tree | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Ladder logic | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Clojure | `-` | dataflow | dataflow | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `battery_ok`: (critical) battery limits (a node sends at most its energy budget allows, sleep below the reserve) in Ada as Eve + APL as KRON witnessed
- - `filter`: (critical) signal filtering (median of 5 then a moving sum, integers) in FORTRAN as Trebago + APL as KRON witnessed
- - `relay`: relay choice (next hop by link margin) in APL as KRON
- - `readings`: reading arrays in APL as KRON
- - `credits`: data sharing credits (readings shared, refusing on overflow) in COBOL as Sutra
- - `nodes`: node tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two neighbouring sensors read 7 and 19: the cell is BOTH
- the test asserts state 3 and that the map shows the anomaly, not an average. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id seine
matrix 382
name Seine
needs peers
needs net
needs location
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

