# diadem — the complete brief

| | |
|---|---|
| matrix layer | 321 |
| icon group | L3 |
| wave spec | `wave27/321-diadem.md` |

## Purpose

runs elections and votes for clubs, co-ops and towns: ballots, eligibility, counting and verifiable results (`spaces.propose`, `spaces.vote`, `spaces.get`, `seats.list`, `journal.query`, `journal.verify`).

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `journal`
- `fs.write`
- `notify`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | election costs | primary | runs |
| FORTRAN | `trebago` | numeric | tally statistics | primary | runs |
| Ada | `eve` | limits | eligibility rules | primary | runs |
| APL | `kron` | arrays | ballot arrays | primary | runs |
| MUMPS | `myra` | records | voter roll tree | primary | runs |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `eligible` (on the roll AND not yet voted AND inside the voting window) in Ada as Eve + APL as KRON witnessed
- `tally` (counts per option over ballot arrays) in APL as KRON + FORTRAN as Trebago witnessed
- `costs` (election costs in minor units) in COBOL as Sutra
- `ballots` arrays in APL as KRON
- `voter_roll` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id diadem
matrix 321
name Diadem
needs peers
needs net
needs journal
needs fs.write
needs notify
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

