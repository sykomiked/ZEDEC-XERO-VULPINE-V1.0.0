# quadrant — the complete brief

| | |
|---|---|
| matrix layer | 394 |
| icon group | L3 |
| wave spec | `wave33/394-quadrant.md` |

## Purpose

Links remote places by satellite: book passes, relay messages and earth images, share ground-station time. Bands and frames (`ehop.bands`, `ehop.seal`, `ehop.open`, `harmonic.encode`, `harmonic.decode`, `freight.pack`, `freight.unpack`), peers (`net.peers`, `peer.status`), images in the Hoard (`files.read`, `files.write`). Satellite pass data and radio hardware have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `net`
- `peers`
- `fs.read`
- `fs.write`
- `location`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | pass billing | primary | runs |
| FORTRAN | `trebago` | numeric | orbit prediction | primary | runs |
| Ada | `eve` | limits | transmit power limits | primary | runs |
| APL | `kron` | arrays | imagery arrays | primary | runs |
| MUMPS | `myra` | records | station tree | primary | runs |
| Elixir | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| AWK | `-` | text | text | primary | planned: needs Lingua front-end |
| MATLAB | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `tx_ok`: (critical) transmit power limits (at most the licensed maximum for the band, only during a booked pass) in Ada as Eve + APL as KRON witnessed
- - `orbit`: (critical) pass prediction (rise, culmination and set minutes from integer orbital elements) in FORTRAN as Trebago + APL as KRON witnessed
- - `relay`: message relay plan (pass by pass) in Ada as Eve
- - `imagery`: imagery arrays in APL as KRON
- - `report`: pass log summaries (passes, bytes per station) in APL as KRON
- - `billing`: pass billing (minor units, refusing on overflow) in COBOL as Sutra
- - `stations`: station tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): one relayed message arrives in two different forms: it is BOTH
- the test asserts state 3 and that both are kept, nothing silently corrupted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id quadrant
matrix 394
name Quadrant
needs net
needs peers
needs fs.read
needs fs.write
needs location
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

