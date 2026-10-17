# mercer — the complete brief

| | |
|---|---|
| matrix layer | 171 |
| icon group | L2 |
| wave spec | `wave14/171-mercer.md` |

## Purpose

runs a small shop: stock, prices, sales and receipts in the books (`ledger.post`, `ledger.balance`, `ledger.entries`) and the Hoard (`files.read`, `files.write`), receipts printed by `print.submit`, market listings from `net.offers`, the seller's card from `player.card`.

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `print`
- `net`
- `peers`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | sales ledger | primary | runs |
| FORTRAN | `trebago` | numeric | pricing model | primary | runs |
| Ada | `eve` | limits | credit limits | primary | runs |
| APL | `kron` | arrays | stock grid | primary | runs |
| MUMPS | `myra` | records | product tree | primary | runs |
| LISP | `lisp` | lists | lists | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **lisp** (LISP): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- stock after a sale (never below zero, refused) in COBOL as Sutra + APL as KRON witnessed
- price from cost and margin (cost x (den + num) / den with the stated rounding) in FORTRAN as Trebago + COBOL as Sutra witnessed
- "sale on credit allowed" (the customer's balance plus the sale at most their declared limit) in Ada as Eve + COBOL as Sutra witnessed
- stock grid in APL as KRON
- product tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id mercer
matrix 171
name Mercer
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs print
needs net
needs peers
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

