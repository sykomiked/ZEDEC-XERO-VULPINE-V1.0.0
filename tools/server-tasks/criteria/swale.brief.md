# swale — the complete brief

| | |
|---|---|
| matrix layer | 226 |
| icon group | L2 |
| wave spec | `wave18/226-swale.md` |

## Purpose

Runs a small bakery: recipes and orders in the Hoard (`files.read`, `files.write`), sales and receipts in the books (`ledger.accounts`, `ledger.entries`, `ledger.post`), statements (`iso20022.render`), receipts printed (`print.submit`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `print`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | orders and receipts | primary | runs |
| FORTRAN | `trebago` | numeric | dough and proofing model | primary | runs |
| Ada | `eve` | limits | allergen limits | primary | runs |
| APL | `kron` | arrays | batch grid | primary | runs |
| MUMPS | `myra` | records | recipe tree | primary | runs |
| Autocode | `-` | formula | formulas | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Autocode): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) batch cost (ingredient qty x unit price, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - (critical) "may sell this order" (the order's excluded allergens and the recipe's allergens share none) in Ada as Eve + MUMPS as MYRA witnessed (MYRA looks the recipe's ingredients up in Kettle's allergen index)
- - dough and proofing model (hydration x100 = water x 10000 / flour
- proof minutes from a dough-temperature table) exact in FORTRAN as Trebago
- - batch grid (orders x products, the batches to bake) in APL as KRON
- - recipe tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the stock count says 12 kg of flour while the sales log implies 4 kg left, giving BOTH
- the test asserts state 3, the stock held (not zeroed) until a recount, and the orders that need flour flagged. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id swale
matrix 226
name Swale
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs print
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

