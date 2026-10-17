# tributary — the complete brief

| | |
|---|---|
| matrix layer | 225 |
| icon group | L2 |
| wave spec | `wave18/225-tributary.md` |

## Purpose

Asks several models the same question and joins their answers by witnessed agreement: local models (`model.info`, `swarm.ask`, `swarm.state`), another node's model (`peer.ask`, priced, owner-approved), energy per answer (`power.status`), scored templates (`play.run`).

## Capabilities (the exact registry names for `needs`)

- `ai.ask`
- `swarm`
- `peers`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | query ledger | primary | runs |
| FORTRAN | `trebago` | numeric | confidence scoring | primary | runs |
| Ada | `eve` | limits | budget caps | primary | runs |
| APL | `kron` | arrays | answer matrix | primary | runs |
| MUMPS | `myra` | records | source tree | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| Haskell | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) agreeing answers counted per normalised answer code in APL as KRON + FORTRAN as Trebago witnessed
- the verdict (all agree TRUE, split BOTH, none NEITHER) joined with `lpres_conjoin` in C
- - (critical) "may ask another model" (tokens and energy within the owner's cap, peer price within its cap) in Ada as Eve + APL as KRON witnessed
- - confidence score x100 per answer (agreeing models / models asked) exact in FORTRAN as Trebago
- - answer matrix (models x answer codes) in APL as KRON
- - query ledger (token cost per query over the D1-D9 faces of kernel/src/swarm/swarm_denom.h, refusing on overflow) in COBOL as Sutra
- - source tree (question, model, answer, time) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): three models answer 42, 42 and 41 with a quorum of 3, giving BOTH
- the test asserts state 3 and that both answers are shown with which model gave which, none picked at random. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id tributary
matrix 225
name Tributary
needs ai.ask
needs swarm
needs peers
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

