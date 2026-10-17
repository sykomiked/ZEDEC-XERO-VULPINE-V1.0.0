# sanctum — the complete brief

| | |
|---|---|
| matrix layer | 295 |
| icon group | L3 |
| wave spec | `wave24/295-sanctum.md` |

## Purpose

custody for valuables and digital assets: who holds what, guardians, withdrawal rules and signed audits (`ledger.accounts`, `ledger.balance`, `ledger.holds`, `recovery.status`, `journal.query`, `journal.verify`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `peers`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | custody ledger | primary | runs |
| FORTRAN | `trebago` | numeric | valuation model | primary | runs |
| Ada | `eve` | limits | withdrawal limits | primary | runs |
| APL | `kron` | arrays | holdings grid | primary | runs |
| MUMPS | `myra` | records | beneficiary tree | primary | runs |
| SAS | `-` | report | reports | primary | planned: needs Lingua front-end |
| Haskell | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (SAS): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `withdraw_ok` (amount at most the per-day limit AND the holder's guardians' approvals met AND no claim open on the asset) in Ada as Eve + COBOL as Sutra witnessed
- `valuation` (holdings at the declared prices in minor units) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `custody_ledger` (holder, asset, quantity, refused on overflow) in COBOL as Sutra
- `holdings` grid in APL as KRON
- `beneficiaries` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id sanctum
matrix 295
name Sanctum
needs ledger.read
needs ledger.post
needs peers
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

