# sunfleck — the complete brief

| | |
|---|---|
| matrix layer | 330 |
| icon group | L3 |
| wave spec | `wave27/330-sunfleck.md` |

## Purpose

plans and verifies tree planting and carbon storage, with growth records that buyers of credits can audit (`place.list`, `files.read`, `files.write`, `ledger.entries`, `ledger.post`, `ledger.hold`).

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | credit sales | primary | runs |
| FORTRAN | `trebago` | numeric | biomass growth model | primary | runs |
| Ada | `eve` | limits | credit issuance limits | primary | runs |
| APL | `kron` | arrays | plot grid | primary | runs |
| MUMPS | `myra` | records | stand and tree hierarchy | primary | runs |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Oberon | `-` | structure | structure | primary | planned: needs Lingua front-end |
| SAS | `-` | report | reports | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Simula 67): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `issue_ok` (credits issued at most the verified carbon held, never twice for one tonne) in Ada as Eve + COBOL as Sutra witnessed
- `biomass` (carbon per stand from counts and girth, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `credit_sales` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `plots` grid in APL as KRON
- `stands` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id sunfleck
matrix 330
name Sunfleck
needs location
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

