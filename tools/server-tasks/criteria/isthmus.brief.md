# isthmus — the complete brief

| | |
|---|---|
| matrix layer | 240 |
| icon group | L2 |
| wave spec | `wave19/240-isthmus.md` |

## Purpose

Mediates disputes: the case room (`pspace.create`, `pspace.post`, `chat.post`, `chat.thread`), the record (`journal.query`, `files.read`, `files.write`), signed copies checked (`web4.verify`), printed agreements (`print.submit`).

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `journal`
- `fs.read`
- `fs.write`
- `print`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | case records | primary | runs |
| FORTRAN | `trebago` | numeric | settlement options | primary | runs |
| Ada | `eve` | limits | procedure limits | primary | runs |
| APL | `kron` | arrays | claim matrix | primary | runs |
| MUMPS | `myra` | records | party tree | primary | runs |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "next step allowed" (both parties heard, cooling-off over, both consented to the proposal) in Ada as Eve + APL as KRON witnessed
- - (critical) settlement split of an amount by agreed proportions (largest remainder, exact, minor units) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - claim matrix (claims x parties: agreed, disputed) in APL as KRON
- - case records (fees and amounts in minor units, refusing on overflow) in COBOL as Sutra
- - party tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): party A says the goods were delivered on 3 May and party B says they were not, giving BOTH
- the test asserts state 3, neither account marked false, and the other claims can still be agreed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `largest_remainder` | num | `pool w1 w2 w3 w4` | sutra | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id isthmus
matrix 240
name Isthmus
needs peers
needs net
needs journal
needs fs.read
needs fs.write
needs print
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

