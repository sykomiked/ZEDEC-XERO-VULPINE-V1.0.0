# macrame — the complete brief

| | |
|---|---|
| matrix layer | 396 |
| icon group | L3 |
| wave spec | `wave33/396-macrame.md` |

## Purpose

Runs a community mesh network: add nodes (`devmesh.invite`, `devmesh.join`, `devmesh.confirm_sas`), share bandwidth (`peer.serve` owner-approved, `net.command`), route around failures (`net.peers`, `sys.net`), and settle shared costs in the books (`ledger.post`, `ledger.entries`), with votes (`spaces.propose`, `spaces.vote`).

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `ledger.read`
- `ledger.post`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | cost sharing | primary | runs |
| FORTRAN | `trebago` | numeric | routing metrics | primary | runs |
| Ada | `eve` | limits | bandwidth caps | primary | runs |
| APL | `kron` | arrays | link matrix | primary | runs |
| MUMPS | `myra` | records | node tree | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- - `cap_ok`: (critical) bandwidth caps (each member within their share, the backbone within its capacity) in Ada as Eve + APL as KRON witnessed
- - `routes`: (critical) routing metrics (expected transmissions per link, shortest path over the link matrix) in FORTRAN as Trebago + APL as KRON witnessed
- - `notice`: route change notices (who hears about a failed link) in Ada as Eve
- - `links`: link matrix in APL as KRON
- - `costs`: cost sharing (largest remainder, minor units, refusing on overflow) in COBOL as Sutra
- - `nodes`: node tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two nodes report different routes to one peer: the route is BOTH
- the test asserts state 3 and that traffic keeps flowing on the known-good link while the conflict is resolved. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `largest_remainder` | num | `pool w1 w2 w3 w4` | sutra | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id macrame
matrix 396
name Macrame
needs peers
needs net
needs ledger.read
needs ledger.post
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

