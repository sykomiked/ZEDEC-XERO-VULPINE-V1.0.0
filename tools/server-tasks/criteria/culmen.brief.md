# culmen — the complete brief

| | |
|---|---|
| matrix layer | 305 |
| icon group | L3 |
| wave spec | `wave25/305-culmen.md` |

## Purpose

one dashboard across an organisation: money, people, risks, projects and AI agents (`ledger.accounts`, `ledger.balance`, `swarm.state`, `spaces.get`, `journal.query`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `swarm`
- `peers`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | consolidated accounts | primary | runs |
| FORTRAN | `trebago` | numeric | forecasting | primary | runs |
| Ada | `eve` | limits | risk appetite limits | primary | runs |
| APL | `kron` | arrays | KPI grid | primary | runs |
| MUMPS | `myra` | records | org chart tree | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Delphi | `-` | structure | structure | primary | planned: needs Lingua front-end |
| PHP | `php` | report | reports | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **-** (Delphi): see docs/LANGUAGE_CATALOGUE.md for this tier
- **php** (PHP): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `risk_ok` (each risk inside the declared appetite) in Ada as Eve + APL as KRON witnessed
- `forecast` (next quarter from the trend) in FORTRAN as Trebago + APL as KRON witnessed
- `consolidated` (accounts summed across departments in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `kpis` grid in APL as KRON
- `org_chart` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id culmen
matrix 305
name Culmen
needs ledger.read
needs swarm
needs peers
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

