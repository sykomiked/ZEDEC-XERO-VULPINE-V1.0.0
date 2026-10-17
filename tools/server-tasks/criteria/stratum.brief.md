# stratum — the complete brief

| | |
|---|---|
| matrix layer | 233 |
| icon group | L2 |
| wave spec | `wave19/233-stratum.md` |

## Purpose

Compares sources side by side, traces citations and flags where they agree or conflict: the Hoard (`search.query`, `files.read`), the owner's Web4 feed (`web4.feed`), summaries from `swarm.ask` (held, read only with Lore's leave), accepted items (`journal.query`).

## Capabilities (the exact registry names for `needs`)

- `search`
- `fs.read`
- `net`
- `ai.ask`
- `journal`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | citation records | primary | runs |
| FORTRAN | `trebago` | numeric | relevance scoring | primary | runs |
| Ada | `eve` | limits | source trust limits | primary | runs |
| APL | `kron` | arrays | claim matrix | primary | runs |
| MUMPS | `myra` | records | reference tree | primary | runs |
| Common Lisp | `-` | symbolic | symbolic reasoning | primary | planned: needs Lingua front-end |
| SQL | `-` | query | queries | primary | planned: needs Lingua front-end |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Common Lisp): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) per claim: sources supporting and contradicting (claim matrix of +1, -1, 0) counted in APL as KRON + FORTRAN as Trebago witnessed
- - relevance score (hits in title, heading and body, weighted) exact in FORTRAN as Trebago
- - source trust limits (trust 0..100, a minimum to count) in Ada as Eve
- - citation records and the reference tree in MUMPS as MYRA
- - Sutra is left out: no money or counted business records here
- citations are MYRA records
- Paraconsistent (BOTH test, in test_zxapp.c): source A supports a claim and source B contradicts it, giving BOTH
- the test asserts state 3 and that both sides are mapped, neither picked. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id stratum
matrix 233
name Stratum
needs search
needs fs.read
needs net
needs ai.ask
needs journal
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

