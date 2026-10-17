# creel — the complete brief

| | |
|---|---|
| matrix layer | 187 |
| icon group | L2 |
| wave spec | `wave15/187-creel.md` |

## Purpose

logs catches, quotas and fishing spots (`place.here`, `place.add`, `place.list`) with weather and sales in the books (`ledger.entries`, `ledger.post`, `ledger.hold`).

## Capabilities (the exact registry names for `needs`)

- `location`
- `ledger.read`
- `ledger.post`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | catch sales | primary | runs |
| FORTRAN | `trebago` | numeric | stock model | primary | runs |
| Ada | `eve` | limits | quota limits | primary | runs |
| APL | `kron` | arrays | catch grid | primary | runs |
| MUMPS | `myra` | records | vessel tree | primary | runs |
| Icon | `-` | pattern | patterns | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Icon): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- quota left (quota minus landed, refused below zero) in COBOL as Sutra + APL as KRON witnessed
- "over quota" in Ada as Eve + APL as KRON witnessed
- catch sales in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- stock trend (landings per season) exact in FORTRAN as Trebago
- catch grid in APL as KRON
- vessel tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id creel
matrix 187
name Creel
needs location
needs ledger.read
needs ledger.post
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

