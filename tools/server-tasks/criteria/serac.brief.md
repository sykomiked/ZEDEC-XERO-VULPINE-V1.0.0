# serac — the complete brief

| | |
|---|---|
| matrix layer | 260 |
| icon group | L2 |
| wave spec | `wave21/260-serac.md` |

## Purpose

Fronts the reserve engines (kernel/src/svault, kernel/src/surplus, kernel/src/vswap, kernel/src/pay/pay_assure.h for the fee split) in plain numbers, read only: the vault and fee buckets (`vault.status`, `vault.check`), the V-Bill dividend pool (`swap.status`, `swap.check`, `swap.quote`), snapshots (`stats.snapshot`, `stats.status`), the books (`ledger.check`, `mesh.books`), auctions and grants (`auction.status`, `grants.status`), the trail (`journal.verify`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `sys.read`
- `journal`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | reserve ledger | primary | runs |
| FORTRAN | `trebago` | numeric | fee split arithmetic | primary | runs |
| Ada | `eve` | limits | reserve-floor limits | primary | runs |
| APL | `kron` | arrays | epoch tables | primary | runs |
| MUMPS | `myra` | records | account records | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- - (critical) the 0.08889% assurance fee (amount x 8889 / 10,000,000) and its split 50% reserve, 25% V-Bill dividend, 15% node bounties, 10% regenerative, summing exactly to the fee with the remainder rule of pay_assure.h, in COBOL as Sutra + FORTRAN as Trebago witnessed (copy the blocks/fee_check pattern)
- - (critical) "reserve at or above the floor" (reserve x floor_den >= floor_num x (N - h) x backing, svault.h S4) in Ada as Eve + COBOL as Sutra witnessed
- - epoch tables (epochs x reserve, pool, dividend) in APL as KRON
- - reserve ledger lines refusing on overflow in COBOL as Sutra
- - account records in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): `vault.status` and the books disagree on the reserve, giving BOTH
- the test (features off) asserts state 3 and that the dividend is shown as not paid until they agree. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id serac
matrix 260
name Serac
needs ledger.read
needs sys.read
needs journal
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

