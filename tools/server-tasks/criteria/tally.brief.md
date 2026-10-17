# tally — the complete brief

| | |
|---|---|
| matrix layer | 109 |
| icon group | L1 |
| wave spec | `wave2/109-tally.md` |

## Purpose

integer and exact rational arithmetic (kernel/src/rational: reduced num/den, overflow refused), units, currency in minor units. Currencies the owner enabled come from `ledger.accounts`. zxvd has no exchange-rate method: use only existing methods; list what's missing in your report. Conversions are shown, never posted.

## Capabilities (the exact registry names for `needs`)

- `ledger.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| COBOL | `sutra` | money | money and ledgers | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| ALGOL 60 | `algol60` | formula | formulas | primary | runs |
| TRAC | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (TRAC): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- rational add, subtract, multiply, divide, reduced, overflow refused, in trebago + sutra witnessed
- currency amount (minor units x rate num / rate den, remainder shown) in sutra + kron witnessed
- unit factors (num/den vectors by unit index, same dimension only) in kron

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id tally
matrix 109
name Tally
needs ledger.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

