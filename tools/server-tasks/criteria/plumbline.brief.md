# plumbline — the complete brief

| | |
|---|---|
| matrix layer | 250 |
| icon group | L2 |
| wave spec | `wave20/250-plumbline.md` |

## Purpose

Fronts the policy engine (kernel/src/policy with policy.c, policy_cobol.c and copybooks; kernel/src/onepolicy; kernel/src/juris with juris.c and juris_vhub.c). Jurisdiction switches (`jurisdiction.get`, `jurisdiction.set`), feature switches and which are regulated (`features.get`, `features.set`), nature-boundary budgets (`power.budget`, `power.status`), the record (`journal.query`). Reading the rulebook and the list of advisory actions have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `journal`
- `ledger.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | rule history | primary | runs |
| FORTRAN | `trebago` | numeric | budget arithmetic | primary | runs |
| Ada | `eve` | limits | hard limits | primary | runs |
| APL | `kron` | arrays | rule matrix | primary | runs |
| MUMPS | `myra` | records | jurisdiction records | primary | runs |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "may this switch change" (a regulated switch needs the owner's session and a confirm
- an advisory action never runs by itself) in Ada as Eve + APL as KRON witnessed
- - (critical) nature-boundary budget left (rolling totals against the budget) in FORTRAN as Trebago + APL as KRON witnessed
- - rule matrix (rules x actions: permit, forbid) in APL as KRON
- - rule history (rule versions and change counts, refusing on overflow) in COBOL as Sutra
- - jurisdiction records in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): rule A permits an action and rule B forbids it, giving BOTH
- the test asserts state 3 and that the action waits for the owner, neither rule winning. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id plumbline
matrix 250
name Plumbline
needs sys.read
needs journal
needs ledger.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

