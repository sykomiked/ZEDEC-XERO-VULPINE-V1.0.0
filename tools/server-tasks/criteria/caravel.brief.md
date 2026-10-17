# caravel — the complete brief

| | |
|---|---|
| matrix layer | 345 |
| icon group | L3 |
| wave spec | `wave29/345-caravel.md` |

## Purpose

Runs a trading stall or shop online and offline: stock and sales in the books (`ledger.post`, `ledger.balance`, `ledger.entries`), escrow (`ledger.hold`, `ledger.holds`, `ledger.release`), market listings (`net.offers`, `net.command` sell), parcels to buyers (`freight.pack`), receipts printed (`print.submit`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `peers`
- `net`
- `print`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | sales ledger | primary | runs |
| FORTRAN | `trebago` | numeric | price curves | primary | runs |
| Ada | `eve` | limits | credit limits | primary | runs |
| APL | `kron` | arrays | inventory grid | primary | runs |
| MUMPS | `myra` | records | supplier tree | primary | runs |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Eiffel): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `release_ok`: (critical) "release the escrow" (buyer confirmed receipt AND seller confirmed dispatch AND the amount matches the listing) in Ada as Eve + COBOL as Sutra witnessed
- - `sale_total`: (critical) sale total with the fee (price x qty plus 8889/10,000,000 of it, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `price_curve`: price curves (markdown by days in stock from a declared table) exact in FORTRAN as Trebago
- - `stock_grid`: inventory grid (items x places) in APL as KRON
- - `suppliers`: supplier tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the buyer's record says 3 items received and the seller's says 4 sent: the sale is BOTH
- the test (scratch books) asserts state 3 and that the money stays on `ledger.hold` until both confirm. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id caravel
matrix 345
name Caravel
needs ledger.read
needs ledger.post
needs peers
needs net
needs print
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

