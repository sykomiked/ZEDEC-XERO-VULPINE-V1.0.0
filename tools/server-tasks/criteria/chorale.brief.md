# chorale — the complete brief

| | |
|---|---|
| matrix layer | 407 |
| icon group | L3 |
| wave spec | `wave34/407-chorale.md` |

## Purpose

Runs a cooperative's or town's governance: proposals and votes (`spaces.propose`, `spaces.vote`, `spaces.get`), debates (`social.post`, `social.feed`, `pspace.post`), budgets in the books (`ledger.accounts`, `ledger.entries`), published decisions (`aipi.put`, `web4.cid`), the record (`journal.query`, `journal.verify`).

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `ledger.read`
- `journal`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | public budget | primary | runs |
| FORTRAN | `trebago` | numeric | turnout modelling | primary | runs |
| Ada | `eve` | limits | quorum limits | primary | runs |
| APL | `kron` | arrays | ballot arrays | primary | runs |
| MUMPS | `myra` | records | member tree | primary | runs |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `quorum_ok`: (critical) quorum limits (quorum met AND the motion's threshold AND the debate period over) in Ada as Eve + APL as KRON witnessed
- - `budget`: (critical) public budget lines (minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - `turnout`: turnout model (turnout per ward per vote) exact in FORTRAN as Trebago
- - `ballots`: ballot arrays in APL as KRON
- - `decisions`: published decision layout (motion, result, votes) in APL as KRON
- - `members`: member tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two motions passed on the same matter contradict: both stand as BOTH
- the test asserts state 3 and that the assembly continues until a rule or vote resolves them. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id chorale
matrix 407
name Chorale
needs peers
needs net
needs ledger.read
needs journal
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

