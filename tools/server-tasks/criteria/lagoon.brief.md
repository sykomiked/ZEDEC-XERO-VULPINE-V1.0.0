# lagoon — the complete brief

| | |
|---|---|
| matrix layer | 270 |
| icon group | L2 |
| wave spec | `wave22/270-lagoon.md` |

## Purpose

Holds savings and guardianship plans for children until they come of age: accounts (`ledger.open_account`, `ledger.balance`, `ledger.entries`, `ledger.post`), escrow (`ledger.hold`, `ledger.holds`, `ledger.release`), the estate plan and guardians (`estate.status`, `recovery.status`), the child's seat (`seats.list`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `screen.time`
- `sys.read`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | trust accounts | primary | runs |
| FORTRAN | `trebago` | numeric | growth model | primary | runs |
| Ada | `eve` | limits | withdrawal limits | primary | runs |
| APL | `kron` | arrays | beneficiary grid | primary | runs |
| MUMPS | `myra` | records | family tree | primary | runs |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |
| Oberon | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Simula 67): see docs/LANGUAGE_CATALOGUE.md for this tier
- **basic** (BASIC): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "withdrawal allowed" (the child is of age, or k guardians approved, AND within the cap) in Ada as Eve + COBOL as Sutra witnessed
- - (critical) trust account balances (minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - projected balance from planned deposits (no rate assumed unless the owner enters num/den) exact in FORTRAN as Trebago
- - beneficiary grid (children x accounts) in APL as KRON
- - family tree in MUMPS as MYRA
- - a child's savings lessons, one step at a time, in BASIC (twin Hatchling)
- - Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`)
- Paraconsistent (BOTH test, in test_zxapp.c): a guardian's claim and the books disagree on a withdrawal, giving BOTH
- the test (scratch books) asserts state 3 and the amount on `ledger.hold` until the guardians resolve it. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id lagoon
matrix 270
name Lagoon
needs ledger.read
needs ledger.post
needs screen.time
needs sys.read
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

