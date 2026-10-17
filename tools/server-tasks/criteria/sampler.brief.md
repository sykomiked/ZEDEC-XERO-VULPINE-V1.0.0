# sampler — the complete brief

| | |
|---|---|
| matrix layer | 347 |
| icon group | L3 |
| wave spec | `wave29/347-sampler.md` |

## Purpose

Catalogues a museum or private collection: objects, provenance, condition reports, loans and exhibition labels in the Hoard (`files.read`, `files.write`, `search.query`), versions (`backup.versions`), the record proven by the journal (`journal.query`, `journal.verify`), labels printed (`print.submit`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `backup`
- `journal`
- `print`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | loan and insurance values | primary | runs |
| FORTRAN | `trebago` | numeric | decay rates | primary | runs |
| Ada | `eve` | limits | humidity limits | primary | runs |
| APL | `kron` | arrays | catalogue arrays | primary | runs |
| MUMPS | `myra` | records | provenance tree | primary | runs |
| Objective-C | `-` | objects | domain objects | primary | planned: needs Lingua front-end |
| SAS | `-` | report | reports | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Objective-C): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `loan_ok`: (critical) "may lend this object" (condition at least the declared grade AND the borrower's humidity within the object's limits AND insured value recorded) in Ada as Eve + APL as KRON witnessed
- - `insured`: (critical) loan and insurance values (minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `decay`: decay rates (condition grade change per year from the condition reports) exact in FORTRAN as Trebago
- - `humidity`: humidity limits (per material, per cent x10) in Ada as Eve
- - `catalogue`: catalogue arrays (objects x fields, counts per gallery) in APL as KRON
- - `provenance`: provenance tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two provenance histories name different owners for 1932: the record is BOTH
- the test asserts state 3 and that both histories stay on the object, neither erased. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id sampler
matrix 347
name Sampler
needs fs.read
needs fs.write
needs search
needs backup
needs journal
needs print
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

