# carrick — the complete brief

| | |
|---|---|
| matrix layer | 360 |
| icon group | L3 |
| wave spec | `wave30/360-carrick.md` |

## Purpose

Binds multiparty commitments into one living contract: text and obligations in the Hoard (`files.read`, `files.write`), every revision on the journal (`journal.query`, `journal.verify`), signatures (`web4.verify`), payments in the books (`ledger.post`, `ledger.hold`, `ledger.holds`, `ledger.release`), keys in escrow (`recovery.status`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `journal`
- `peers`
- `ledger.read`
- `ledger.post`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | obligation ledger | primary | runs |
| FORTRAN | `trebago` | numeric | penalty schedules | primary | runs |
| Ada | `eve` | limits | limit clauses | primary | runs |
| APL | `kron` | arrays | obligation matrix | primary | runs |
| MUMPS | `myra` | records | party tree | primary | runs |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Eiffel): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `limit_ok`: (critical) limit clauses (a payment or penalty at most the clause's cap AND inside its period) in Ada as Eve + COBOL as Sutra witnessed
- - `penalty`: (critical) penalty schedules (days late x rate, capped, minor units) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - `ledger`: obligation ledger (due, paid, held, refusing on overflow) in COBOL as Sutra
- - `matrix`: obligation matrix (parties x obligations, four values) in APL as KRON
- - `parties`: party tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): obligation 3 requires delivery by 1 May and obligation 7 forbids it before 10 May: the pair is BOTH
- the test (scratch books) asserts state 3 and that only those obligations go to escrow while the rest of the contract stands. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id carrick
matrix 360
name Carrick
needs fs.read
needs fs.write
needs journal
needs peers
needs ledger.read
needs ledger.post
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

