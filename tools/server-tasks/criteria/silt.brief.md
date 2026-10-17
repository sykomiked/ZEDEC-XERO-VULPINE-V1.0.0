# silt — the complete brief

| | |
|---|---|
| matrix layer | 242 |
| icon group | L2 |
| wave spec | `wave20/242-silt.md` |

## Purpose

Receives, stamps, files and retains official records with a signed trail: records in the Hoard (`files.list`, `files.stat`, `files.read`, `files.write`, `files.mkdir`; disposal only to the Trash with `files.trash` after approval), versions (`backup.versions`), the trail (`journal.query`, `journal.verify`), signed copies checked (`web4.verify`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `journal`
- `backup`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | register of records | primary | runs |
| FORTRAN | `trebago` | numeric | backlog forecasting | primary | runs |
| Ada | `eve` | limits | retention limits | primary | runs |
| APL | `kron` | arrays | filing grid | primary | runs |
| MUMPS | `myra` | records | case file tree | primary | runs |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Haskell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "may dispose" (retention period over AND no legal hold AND the records officer approved) in Ada as Eve + APL as KRON witnessed
- - backlog forecast (arrivals minus processed per day, days to clear) exact in FORTRAN as Trebago
- - filing grid (series x years) in APL as KRON
- - register of records (register numbers per year, refusing on overflow) in COBOL as Sutra
- - case file tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two records with the same reference and different dates give BOTH
- the test asserts state 3 and that both are filed and flagged, never merged. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id silt
matrix 242
name Silt
needs fs.read
needs fs.write
needs journal
needs backup
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

