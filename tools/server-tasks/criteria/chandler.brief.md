# chandler — the complete brief

| | |
|---|---|
| matrix layer | 161 |
| icon group | L2 |
| wave spec | `wave13/161-chandler.md` |

## Purpose

keeps vessel logs, tides, crew and provisions for boats and small fleets in the Hoard (`files.read`, `files.write`), with places (`place.here`, `place.list`) and radio messages (`harmonic.encode`). zxvd has no method for tide predictions source: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | provision accounts | primary | runs |
| FORTRAN | `trebago` | numeric | tide model | primary | runs |
| Ada | `eve` | limits | load limits | primary | runs |
| APL | `kron` | arrays | tide tables | primary | runs |
| MUMPS | `myra` | records | crew and vessel tree | primary | runs |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |
| Java | `java` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Occam): see docs/LANGUAGE_CATALOGUE.md for this tier
- **java** (Java): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- provisions (days of supply = stock / (crew x daily ration), floor) and provision spend in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- tide height at a time by the rule of twelfths from high and low water (millimetres) in FORTRAN as Trebago + APL as KRON witnessed
- "within load limits" (cargo, stores and crew at most the vessel's declared maximum) in Ada as Eve + APL as KRON witnessed
- tide tables in APL as KRON
- crew and vessel tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id chandler
matrix 161
name Chandler
needs location
needs fs.read
needs fs.write
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

