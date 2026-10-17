# aureole — the complete brief

| | |
|---|---|
| matrix layer | 277 |
| icon group | L3 |
| wave spec | `wave23/277-aureole.md` |

## Purpose

runs a town or co-op council: agendas, motions, minutes, budgets and signed decisions in one public record (`spaces.propose`, `spaces.vote`, `spaces.get`, `journal.query`, `web4.verify`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `journal`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | municipal budget | primary | runs |
| FORTRAN | `trebago` | numeric | revenue projections | primary | runs |
| Ada | `eve` | limits | quorum rules | primary | runs |
| APL | `kron` | arrays | vote tallies | primary | runs |
| MUMPS | `myra` | records | bylaw hierarchy | primary | runs |
| PHP | `php` | report | reports | primary | planned: SDK host only, no in-app runtime yet |
| Haskell | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **php** (PHP): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Haskell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `motion_passes` (quorum present AND votes for over the declared share of votes cast) in Ada as Eve + APL as KRON witnessed
- `vote_tally` (counts per option over the ballot vector) in APL as KRON + FORTRAN as Trebago witnessed
- `budget_lines` (municipal budget per line in minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `revenue` (next year's revenue from the trend) in FORTRAN as Trebago
- `bylaws` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |

## The manifest contract

```
zxapp 1
id aureole
matrix 277
name Aureole
needs peers
needs net
needs journal
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

