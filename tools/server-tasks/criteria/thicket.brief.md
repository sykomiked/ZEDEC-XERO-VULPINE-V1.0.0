# thicket — the complete brief

| | |
|---|---|
| matrix layer | 262 |
| icon group | L2 |
| wave spec | `wave21/262-thicket.md` |

## Purpose

Fronts community updates (kernel/src/peer_audit, kernel/src/evolve with evo_upd.c, evo_lineage.c and evo_trust.c, kernel/src/update). Signed update check (`update.check`), proposals and votes (`spaces.propose`, `spaces.vote`), the circle's discussion (`social.feed`, `social.post`), test runs (`play.run`), signatures (`web4.verify`), the trail (`journal.query`), versions (`backup.versions`). Listing forks and peer audits, submitting an audit and installing a reviewed update have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `net`
- `peers`
- `sys.read`
- `journal`
- `backup`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | proposal ledger | primary | runs |
| FORTRAN | `trebago` | numeric | test score arithmetic | primary | runs |
| Ada | `eve` | limits | refuses an update without an audit | primary | runs |
| APL | `kron` | arrays | review matrix | primary | runs |
| MUMPS | `myra` | records | fork tree | primary | runs |
| SAS | `-` | report | reports | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | witness | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (SAS): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "may accept" = an audit present AND the auditors agree AND tests TRUE AND both signatures valid, in Ada as Eve + APL as KRON witnessed
- - (critical) test score x100 (weighted passes per suite) in FORTRAN as Trebago + APL as KRON witnessed
- - review matrix (auditors x checks) in APL as KRON
- - proposal ledger (proposal numbers and vote counts, refusing on overflow) in COBOL as Sutra
- - fork tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): auditor A passes an update and auditor B fails it, giving BOTH
- the test asserts state 3 and that it is not installed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id thicket
matrix 262
name Thicket
needs net
needs peers
needs sys.read
needs journal
needs backup
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

