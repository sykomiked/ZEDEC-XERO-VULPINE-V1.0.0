# tallystick — the complete brief

| | |
|---|---|
| matrix layer | 78 |
| icon group | L1 |
| wave spec | `wave4/078-tallystick.md` |

## Purpose

make invoices and receipts, record payments, render statements (`ledger.post`, `ledger.entries`, `iso20022.render` camt.053), save and print them through the Banker (`files.write`, `print.submit`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `print`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | money and ledgers | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| Ada | `eve` | limits | limits | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **-** (Eiffel): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- invoice total (qty x unit price per line, tax at num/den with the stated rounding) in sutra + trebago witnessed
- "paid in full" truth (payments at or above total) in eve + sutra witnessed
- invoice number sequence as a fixed business record in sutra (refuses on overflow)
- Recording a payment needs the owner's confirmation

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id tallystick
matrix 78
name Tallystick
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs print
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

