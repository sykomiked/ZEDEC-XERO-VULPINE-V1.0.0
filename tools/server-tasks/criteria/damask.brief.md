# damask — the complete brief

| | |
|---|---|
| matrix layer | 355 |
| icon group | L3 |
| wave spec | `wave29/355-damask.md` |

## Purpose

Runs a restaurant kitchen: menus, recipes, orders, allergens and stock in the Hoard (`files.read`, `files.write`, `search.query`), supplier payments and sales in the books (`ledger.post`, `ledger.entries`, `ledger.balance`), tickets printed (`print.submit`), shift times (`sys.info`). Fridge sensors have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `ledger.read`
- `ledger.post`
- `print`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | supplier payments | primary | runs |
| FORTRAN | `trebago` | numeric | portion costing | primary | runs |
| Ada | `eve` | limits | fridge temperature limits | primary | runs |
| APL | `kron` | arrays | menu grid | primary | runs |
| MUMPS | `myra` | records | allergen tree | primary | runs |
| Laning and Zierler | `-` | formula | formulas | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Laning and Zierler): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `serve_ok`: (critical) "may serve this dish" (label allergens and recipe allergens agree AND none excluded by the order) in Ada as Eve + MUMPS as MYRA witnessed (MYRA looks the recipe up in the allergen tree)
- - `supplier_pay`: (critical) supplier payments (invoice lines in minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `portion`: portion costing (cost per plate from the scaled recipe) exact in FORTRAN as Trebago
- - `fridge_ok`: fridge temperature limits (tenths of a degree, time out of range) in Ada as Eve
- - `menu`: menu grid (dishes x services) in APL as KRON
- - `allergens`: allergen tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a dish's label lists no nuts while its recipe includes almond flour: the dish is BOTH
- the test asserts state 3 and that it is blocked from serving until resolved. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id damask
matrix 355
name Damask
needs fs.read
needs fs.write
needs search
needs ledger.read
needs ledger.post
needs print
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

