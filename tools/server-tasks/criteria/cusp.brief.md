# cusp — the complete brief

| | |
|---|---|
| matrix layer | 287 |
| icon group | L3 |
| wave spec | `wave24/287-cusp.md` |

## Purpose

manages livestock: herd records, breeding, grazing rotation, vet visits and sales (`ledger.entries`, `ledger.post`, `net.offers`, `files.read`, `files.write`, `place.list`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `location`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | livestock sales | primary | runs |
| FORTRAN | `trebago` | numeric | pasture growth model | primary | runs |
| Ada | `eve` | limits | medicine withdrawal limits | primary | runs |
| APL | `kron` | arrays | paddock grid | primary | runs |
| MUMPS | `myra` | records | animal pedigree tree | primary | runs |
| ABAP | `-` | business | business records | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (ABAP): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `sale_ok` (an animal may be sold only after every treatment's withdrawal period is over) in Ada as Eve + APL as KRON witnessed
- `pasture` (grass growth per paddock per day and days of grazing left) in FORTRAN as Trebago + APL as KRON witnessed
- `livestock_sales` (head x price in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `paddocks` grid in APL as KRON
- `pedigree` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## The manifest contract

```
zxapp 1
id cusp
matrix 287
name Cusp
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs location
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

