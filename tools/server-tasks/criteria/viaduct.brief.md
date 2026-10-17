# viaduct — the complete brief

| | |
|---|---|
| matrix layer | 327 |
| icon group | L3 |
| wave spec | `wave27/327-viaduct.md` |

## Purpose

settles payments between currencies, co-ops and networks with fair rates and clear records (`ledger.accounts`, `ledger.post`, `ledger.hold`, `ledger.release`, `netting.open`, `netting.positions`, `netting.close`, `swap.quote`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `peers`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | settlement ledger | primary | runs |
| FORTRAN | `trebago` | numeric | exchange rate model | primary | runs |
| Ada | `eve` | limits | transfer limits | primary | runs |
| APL | `kron` | arrays | currency grid | primary | runs |
| MUMPS | `myra` | records | counterparty tree | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `transfer_ok` (amount at most the per-day limit AND the counterparty known AND the rate inside its declared band) in Ada as Eve + COBOL as Sutra witnessed
- `exchange` (amount x rate num / rate den with the remainder carried) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `settlement_ledger` (in minor units, refused on overflow) in COBOL as Sutra
- `currencies` grid in APL as KRON
- `counterparties` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id viaduct
matrix 327
name Viaduct
needs ledger.read
needs ledger.post
needs peers
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

