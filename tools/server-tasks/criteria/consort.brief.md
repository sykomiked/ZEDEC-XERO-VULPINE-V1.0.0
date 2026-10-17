# consort — the complete brief

| | |
|---|---|
| matrix layer | 392 |
| icon group | L3 |
| wave spec | `wave33/392-consort.md` |

## Purpose

Finds and connects people with shared skills into project teams, mentoring pairs and trusted introductions: the mesh map (`net.peers`, `peer.status`), contacts (`files.read`, `files.write`), introductions as signed envelopes held for consent (`web4.post`, `web4.consent`, `web4.verify`), the circle (`social.groups`, `social.post`).

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | contribution ledger | primary | runs |
| FORTRAN | `trebago` | numeric | match scoring | primary | runs |
| Ada | `eve` | limits | contact limits | primary | runs |
| APL | `kron` | arrays | skill matrix | primary | runs |
| MUMPS | `myra` | records | network tree | primary | runs |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |
| Oberon | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `contact_ok`: (critical) contact limits (an introduction only when both opted in AND at most n a week AND neither blocked the other) in Ada as Eve + APL as KRON witnessed
- - `match`: (critical) match scoring (shared skills and availability weighted, integers) in FORTRAN as Trebago + APL as KRON witnessed
- - `intro`: introduction routing (who is told what) in Ada as Eve
- - `skills`: skill matrix in APL as KRON
- - `contrib`: contribution ledger (refusing on overflow) in COBOL as Sutra
- - `network`: network tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two references about one person conflict: it is BOTH
- the test asserts state 3 and that both are shown with their sources. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id consort
matrix 392
name Consort
needs peers
needs net
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

