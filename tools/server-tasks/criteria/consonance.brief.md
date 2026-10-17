# consonance — the complete brief

| | |
|---|---|
| matrix layer | 352 |
| icon group | L3 |
| wave spec | `wave29/352-consonance.md` |

## Purpose

Negotiates, drafts and signs agreements between groups or nations: drafts in the Hoard (`files.read`, `files.write`), revisions on the journal (`journal.query`, `journal.verify`), signatures checked (`web4.verify`), translation (`xlate.translate`, `xlate.detect`), the negotiating room (`pspace.create`, `pspace.post`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `journal`
- `peers`
- `net`
- `ai.ask`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | treaty obligations ledger | primary | runs |
| FORTRAN | `trebago` | numeric | quota modelling | primary | runs |
| Ada | `eve` | limits | binding limits | primary | runs |
| APL | `kron` | arrays | clause matrix | primary | runs |
| MUMPS | `myra` | records | party hierarchy | primary | runs |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Eiffel): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `binding_ok`: (critical) "binding" (every party signed the same text version AND every clause reading agreed AND ratified by each party's declared rule) in Ada as Eve + APL as KRON witnessed
- - `quota`: (critical) quota modelling (shares of a total by agreed weights, largest remainder, exact) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - `obligations`: treaty obligations ledger (amounts and due dates, refusing on overflow) in COBOL as Sutra
- - `clauses`: clause matrix (clauses x parties, four values) in APL as KRON
- - `parties`: party hierarchy in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): party A reads clause 4 as a ceiling and party B as a floor: the clause is BOTH
- the test asserts state 3, the clause held in escrow, and the rest of the agreement still standing until arbiters rule. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `largest_remainder` | num | `pool w1 w2 w3 w4` | sutra | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id consonance
matrix 352
name Consonance
needs fs.read
needs fs.write
needs journal
needs peers
needs net
needs ai.ask
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

