# tradewind — the complete brief

| | |
|---|---|
| matrix layer | 211 |
| icon group | L2 |
| wave spec | `wave17/211-tradewind.md` |

## Purpose

plans routes for trips and shipments from saved places (`place.here`, `place.list`, `place.add`), weather and address lookups, kept in the Hoard (`files.read`, `files.write`). zxvd has no method for road network and address geocoding: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | waybills | primary | runs |
| FORTRAN | `trebago` | numeric | route optimisation | primary | runs |
| Ada | `eve` | limits | load and hours limits | primary | runs |
| APL | `kron` | arrays | distance matrix | primary | runs |
| MUMPS | `myra` | records | stop hierarchy | primary | runs |
| K | `kron` | query | queries | primary | runs |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- best order of stops (shortest total over the distance matrix, at most 8 stops) in FORTRAN as Trebago + APL as KRON witnessed
- "within load and hours limits" (load at most the vehicle's, driving minutes at most the declared) in Ada as Eve + APL as KRON witnessed
- stop filters as K queries in APL as KRON
- waybills (waybill number, charged weight, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- stop hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id tradewind
matrix 211
name Tradewind
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

