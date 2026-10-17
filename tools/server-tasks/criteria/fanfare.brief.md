# fanfare — the complete brief

| | |
|---|---|
| matrix layer | 364 |
| icon group | L3 |
| wave spec | `wave30/364-fanfare.md` |

## Purpose

Recognises contributions in a team or community with badges, credits and an auditable reputation: the circle (`social.feed`, `social.post`), votes (`spaces.propose`, `spaces.vote`), credits in the books (`ledger.post`, `ledger.hold`, `ledger.holds`, `ledger.release`), announcements (`chat.post`, `pspace.post`).

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `ledger.read`
- `ledger.post`
- `swarm`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | credit ledger | primary | runs |
| FORTRAN | `trebago` | numeric | reputation scoring | primary | runs |
| Ada | `eve` | limits | anti-gaming limits | primary | runs |
| APL | `kron` | arrays | contribution matrix | primary | runs |
| MUMPS | `myra` | records | member tree | primary | runs |
| PHP | `php` | report | reports | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **php** (PHP): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `gaming_ok`: (critical) anti-gaming limits (self-award refused, at most n awards from one giver a week, reciprocal pairs flagged) in Ada as Eve + APL as KRON witnessed
- - `reputation`: (critical) reputation score (contributions weighted by kind, decaying by a declared half-life table) in FORTRAN as Trebago + APL as KRON witnessed
- - `credits`: credit ledger (minor units, refusing on overflow) in COBOL as Sutra
- - `report`: public contribution report (counts per member per kind) in APL as KRON
- - `members`: member tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): a contribution claimed by two members: it is BOTH
- the test (scratch books) asserts state 3 and that its credit is held, neither given nor denied. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id fanfare
matrix 364
name Fanfare
needs peers
needs net
needs ledger.read
needs ledger.post
needs swarm
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

