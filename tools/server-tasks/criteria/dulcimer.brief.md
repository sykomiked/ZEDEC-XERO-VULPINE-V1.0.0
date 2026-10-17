# dulcimer — the complete brief

| | |
|---|---|
| matrix layer | 342 |
| icon group | L3 |
| wave spec | `wave28/342-dulcimer.md` |

## Purpose

designs pieces, tracks metal and stones, schedules bench work and sells finished goods (`files.read`, `files.write`, `ledger.entries`, `ledger.post`, `net.offers`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `camera`
- `net`
- `peers`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | workshop orders | primary | runs |
| FORTRAN | `trebago` | numeric | alloy mix | primary | runs |
| Ada | `eve` | limits | furnace temperature limits | primary | runs |
| APL | `kron` | arrays | stock grid | primary | runs |
| MUMPS | `myra` | records | piece lineage tree | primary | runs |
| Bourne shell | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| Kotlin | `kotlin` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Bourne shell): see docs/LANGUAGE_CATALOGUE.md for this tier
- **kotlin** (Kotlin): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `furnace_ok` (melt temperature at most the furnace's and the alloy's declared maximum) in Ada as Eve + APL as KRON witnessed
- `alloy_mix` (grams of each metal for a fineness, exact) in FORTRAN as Trebago + APL as KRON witnessed
- `orders` (workshop orders in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `stock` grid in APL as KRON
- `pieces` lineage tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id dulcimer
matrix 342
name Dulcimer
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs camera
needs net
needs peers
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

