# commonweal — the complete brief

| | |
|---|---|
| matrix layer | 387 |
| icon group | L3 |
| wave spec | `wave32/387-commonweal.md` |

## Purpose

Your part in the mutual-aid economy in one honest view, read only: bounties earned (`swarm.earnings`, `peer.status`, `mesh.books`), patronage grants (`grants.status`, `grants.account`), Vino routes (`swap.status`, `swap.quote`), the reserve and the dividend (`vault.status`, `vault.check`, `swap.check`), your books (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.check`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `swarm`
- `peers`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | household ledger | primary | runs |
| FORTRAN | `trebago` | numeric | share arithmetic | primary | runs |
| Ada | `eve` | limits | advisory limits | primary | runs |
| APL | `kron` | arrays | epoch tables | primary | runs |
| MUMPS | `myra` | records | holdings tree | primary | runs |
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

- - `advisory_ok`: (critical) advisory limits (Commonweal posts nothing
- every action it suggests is shown as a step the owner takes in Coffer, Gavel or Crown) in Ada as Eve + APL as KRON witnessed
- - `shares`: (critical) your share of the dividend (your weight / total weight x the pool, largest remainder, minor units) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - `household`: household ledger view (grants, bounties, routes, dividend per epoch, refusing on overflow) in COBOL as Sutra
- - `epochs`: epoch tables in APL as KRON
- - `holdings`: holdings tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): your books and `mesh.books` disagree on last epoch's bounties: the figure is BOTH
- the test (features off) asserts state 3 and that both are shown, never netted away. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `largest_remainder` | num | `pool w1 w2 w3 w4` | sutra | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id commonweal
matrix 387
name Commonweal
needs ledger.read
needs swarm
needs peers
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

