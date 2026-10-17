# pannier — the complete brief

| | |
|---|---|
| matrix layer | 150 |
| icon group | L2 |
| wave spec | `wave12/150-pannier.md` |

## Purpose

carries messages and data parcels store-and-forward between peers (`net.peers`, `peer.status`), cut into freights that survive lost packets (`freight.pack`, `freight.unpack`), with the queue in the Hoard (`files.read`, `files.write`). zxvd has no method for a store-and-forward relay queue in the daemon: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | delivery receipts | primary | runs |
| FORTRAN | `trebago` | numeric | route timing | primary | runs |
| Ada | `eve` | limits | retry limits | primary | runs |
| APL | `kron` | arrays | queue arrays | primary | runs |
| MUMPS | `myra` | records | parcel tree | primary | runs |
| Elixir | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| SNOBOL | `-` | text | text | primary | planned: needs Lingua front-end |
| C# | `dotnet` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier
- **dotnet** (C#): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "send now" = peer active AND parcel within size AND attempt at most the retry limit, in Ada as Eve + APL as KRON witnessed
- retry limits (at most 16 attempts, back-off capped at 3600 s) in Ada as Eve
- delivery receipts (sequence per peer strictly rising, refused on reuse or overflow) in COBOL as Sutra + APL as KRON witnessed
- route timing (expected hours as the sum of hop times) exact in FORTRAN as Trebago
- the queue as a bounded ring (head - tail >= capacity is backpressure, oldest first) in APL as KRON
- parcel tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |

## The manifest contract

```
zxapp 1
id pannier
matrix 150
name Pannier
needs peers
needs net
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

