# carapace — the complete brief

| | |
|---|---|
| matrix layer | 383 |
| icon group | L3 |
| wave spec | `wave32/383-carapace.md` |

## Purpose

Manages a fishing fleet or co-op: catch logs and quotas (`place.here`, `place.list`), landing payments in the books (`ledger.post`, `ledger.entries`, `ledger.hold`, `ledger.holds`, `ledger.release`), traceable labels printed (`print.submit`), market listings (`net.offers`).

## Capabilities (the exact registry names for `needs`)

- `location`
- `ledger.read`
- `ledger.post`
- `print`
- `net`
- `peers`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | landing payments | primary | runs |
| FORTRAN | `trebago` | numeric | fish stock models | primary | runs |
| Ada | `eve` | limits | quota limits | primary | runs |
| APL | `kron` | arrays | catch grid | primary | runs |
| MUMPS | `myra` | records | vessel tree | primary | runs |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |
| PostScript | `-` | layout | page layout | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Occam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `pay_ok`: (critical) "pay this landing" (log and landing weight agree within the declared margin AND quota left) in Ada as Eve + COBOL as Sutra witnessed
- - `payment`: (critical) landing payments (kg x price per species, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `stocks`: fish stock models (landings and effort per season, catch per unit effort) in FORTRAN as Trebago
- - `catch`: catch grid in APL as KRON
- - `labels`: traceable label layout (vessel, area, date, species) in APL as KRON
- - `vessels`: vessel tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the catch log says 410 kg and the landing scale 352 kg: the landing is BOTH
- the test (scratch books) asserts state 3 and the payment held until explained. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id carapace
matrix 383
name Carapace
needs location
needs ledger.read
needs ledger.post
needs print
needs net
needs peers
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

