# lode — the complete brief

| | |
|---|---|
| matrix layer | 246 |
| icon group | L2 |
| wave spec | `wave20/246-lode.md` |

## Purpose

Appraises and registers precious items with photos, provenance and value records: photos and records in the Hoard (`files.read`, `files.write`, room from `sys.disk`), the trail (`journal.query`), values in the books (`ledger.accounts`, `ledger.balance`, `ledger.post`).

## Capabilities (the exact registry names for `needs`)

- `camera`
- `fs.read`
- `fs.write`
- `sys.read`
- `journal`
- `ledger.read`
- `ledger.post`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | valuation records | primary | runs |
| FORTRAN | `trebago` | numeric | carat and cut metrics | primary | runs |
| Ada | `eve` | limits | grading rules | primary | runs |
| APL | `kron` | arrays | grade matrix | primary | runs |
| MUMPS | `myra` | records | provenance tree | primary | runs |
| Julia | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| Haskell | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Julia): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) grade from measured inputs within the table's bands in Ada as Eve + APL as KRON witnessed
- - (critical) value recorded (minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - carat and cut metrics (carats x100 = milligrams / 2
- proportions x1000) exact in FORTRAN as Trebago
- - grade matrix (items x grading inputs) in APL as KRON
- - provenance tree (item, holder, from, to, evidence) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two provenance chains name different holders for the same item, giving BOTH
- the test asserts state 3 and that both stay on the register, neither erased. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id lode
matrix 246
name Lode
needs camera
needs fs.read
needs fs.write
needs sys.read
needs journal
needs ledger.read
needs ledger.post
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

