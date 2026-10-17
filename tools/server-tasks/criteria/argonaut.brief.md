# argonaut — the complete brief

| | |
|---|---|
| matrix layer | 367 |
| icon group | L3 |
| wave spec | `wave30/367-argonaut.md` |

## Purpose

Grows savings in chambered goals that compound over time, with locked portions: accounts (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.open_account`, `ledger.post`), holds for locked chambers (`ledger.hold`, `ledger.holds`, `ledger.release`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | savings ledger | primary | runs |
| FORTRAN | `trebago` | numeric | integer compound interest | primary | runs |
| Ada | `eve` | limits | withdrawal limits | primary | runs |
| APL | `kron` | arrays | goal grid | primary | runs |
| MUMPS | `myra` | records | household tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- - `withdraw_ok`: (critical) withdrawal limits (an emergency chamber opens only on the declared conditions
- a future chamber only after its date) in Ada as Eve + COBOL as Sutra witnessed
- - `compound`: (critical) integer compound growth per chamber (balance x (den + num) / den per period, floor, remainder carried) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - `ledger`: savings ledger (minor units, refusing on overflow) in COBOL as Sutra
- - `goals`: goal grid (chambers x months) in APL as KRON
- - `household`: household tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the bank statement says 1,200.00 and the app's books 1,150.00: the gap is BOTH
- the test (scratch books) asserts state 3 and the 50.00 held, never written off. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id argonaut
matrix 367
name Argonaut
needs ledger.read
needs ledger.post
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

