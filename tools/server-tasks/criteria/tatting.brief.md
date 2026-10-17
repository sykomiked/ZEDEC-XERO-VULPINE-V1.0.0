# tatting — the complete brief

| | |
|---|---|
| matrix layer | 372 |
| icon group | L3 |
| wave spec | `wave31/372-tatting.md` |

## Purpose

Keeps vaccines, food and samples cold from source to door: consignments and sensor logs in the Hoard (`files.read`, `files.write`), cold archives (`backup.run`, `backup.list`, `backup.verify`), parcels (`freight.pack`, `net.peers`, `peer.status`), places (`place.list`). Temperature loggers have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `backup`
- `sys.read`
- `peers`
- `net`
- `location`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | shipment billing | primary | runs |
| FORTRAN | `trebago` | numeric | thermal decay model | primary | runs |
| Ada | `eve` | limits | temperature limits | primary | runs |
| APL | `kron` | arrays | sensor arrays | primary | runs |
| MUMPS | `myra` | records | consignment tree | primary | runs |
| JOVIAL | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Scala | `-` | dataflow | dataflow | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (JOVIAL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `deliver_ok`: (critical) "deliver" (every log reading within the product's range AND the time above range at most the declared minutes) in Ada as Eve + APL as KRON witnessed
- - `decay`: (critical) thermal decay (remaining shelf minutes from time-temperature exposure, integer table) in FORTRAN as Trebago + APL as KRON witnessed
- - `sensors`: sensor arrays (loggers x minutes) in APL as KRON
- - `alert`: spoilage alert routing (consignment to its handlers) in Ada as Eve
- - `billing`: shipment billing (minor units, refusing on overflow) in COBOL as Sutra
- - `consignments`: consignment tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two loggers in one box disagree on an excursion: the shipment is BOTH
- the test asserts state 3 and that it is quarantined, neither thrown out nor delivered. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id tatting
matrix 372
name Tatting
needs fs.read
needs fs.write
needs backup
needs sys.read
needs peers
needs net
needs location
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

