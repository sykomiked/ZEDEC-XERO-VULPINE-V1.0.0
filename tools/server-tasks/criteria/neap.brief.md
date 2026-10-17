# neap — the complete brief

| | |
|---|---|
| matrix layer | 234 |
| icon group | L2 |
| wave spec | `wave19/234-neap.md` |

## Purpose

Keeps double-entry books, reconciles accounts and prepares tax reports exactly: accounts and entries (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.check`), suspense holds (`ledger.hold`, `ledger.holds`, `ledger.release`), statements (`iso20022.render` camt.053), the jurisdiction (`jurisdiction.get`), reports saved and printed (`files.write`, `print.submit`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `fs.write`
- `print`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | general ledger | primary | runs |
| FORTRAN | `trebago` | numeric | depreciation model | primary | runs |
| Ada | `eve` | limits | posting rules | primary | runs |
| APL | `kron` | arrays | account grid | primary | runs |
| MUMPS | `myra` | records | chart-of-accounts tree | primary | runs |
| RPG | `-` | report | reports | primary | planned: needs Lingua front-end |
| TRAC | `-` | text | text | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (RPG): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "entry may post" (debits equal credits, every account open, period open) in Ada as Eve + COBOL as Sutra witnessed
- - (critical) A = L + E after every post in COBOL as Sutra + FORTRAN as Trebago witnessed
- - depreciation (straight line and declining balance in minor units, the remainder in the last period) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - general ledger lines refusing on overflow in COBOL as Sutra
- - account grid (accounts x months) in APL as KRON
- - chart-of-accounts tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a statement line of 250.00 and no matching book entry give BOTH
- the test (scratch books) asserts state 3, the amount on a suspense `ledger.hold`, and A = L + E still holding. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id neap
matrix 234
name Neap
needs ledger.read
needs ledger.post
needs fs.write
needs print
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

