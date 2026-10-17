# cabochon — the complete brief

| | |
|---|---|
| matrix layer | 278 |
| icon group | L3 |
| wave spec | `wave23/278-cabochon.md` |

## Purpose

gathers accounts, savings goals, bills and investments into one honest picture of your wealth (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.holds`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | household books | primary | runs |
| FORTRAN | `trebago` | numeric | compound growth model | primary | runs |
| Ada | `eve` | limits | spending caps | primary | runs |
| APL | `kron` | arrays | portfolio grid | primary | runs |
| MUMPS | `myra` | records | family account tree | primary | runs |
| Objective-C | `-` | objects | domain objects | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Objective-C): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `net_worth` (assets minus liabilities in minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `goal_months` (months to a savings goal at the declared monthly saving and rate, floor) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `cap_ok` (spending in a category at most its declared cap) in Ada as Eve + APL as KRON witnessed
- `portfolio` grid in APL as KRON
- `family_accounts` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id cabochon
matrix 278
name Cabochon
needs ledger.read
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

