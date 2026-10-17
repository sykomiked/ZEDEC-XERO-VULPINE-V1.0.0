# vintner — the complete brief

| | |
|---|---|
| matrix layer | 91 |
| icon group | L1 |
| wave spec | `wave1/091-vintner.md` |

## Purpose

vineyard lots, barrels, bottling and sales in the books (`ledger.open_account`, `ledger.post`, `ledger.entries`, `ledger.balance`); barrel lot labels from `grants.status`.

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| MUMPS | `myra` | records | records | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| COBOL | `sutra` | money | money and ledgers | primary | runs |
| C++ | `cpp` | money | money and ledgers | witness | runs (compiled to wasm) |
| Forth | `forth` | stack | small interpreters | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **cpp** (C++): see docs/LANGUAGE_CATALOGUE.md for this tier
- **forth** (Forth): the run tier: stack words. '<entry> ( args -- result )' with integer words. Range laws are natural here.

## The spec's Logic clauses (what each component computes)

- sale value (bottles x price, minor units) in sutra + C++ wasm witnessed
- volume through the stages (harvest, must, barrel, bottle, losses) conserved exactly in trebago
- cellar probe readings (raw counts to tenths of a degree, range check) in forth
- lots and vintages in myra
- Posting a sale needs the owner's confirmation

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `within_range` | truth | `value min max` | eve | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| `conserved_sum` | truth | `whole a b c d` | trebago | `check conserved_sum whole=100,100,100,101 a=25,0,0,25 b=25,0,0,25 c=25,0,0,25 d=25,100,99,25 = true,true,false,false` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id vintner
matrix 91
name Vintner
needs ledger.read
needs ledger.post
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

