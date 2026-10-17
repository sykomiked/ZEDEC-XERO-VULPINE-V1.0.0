# vernier — the complete brief

| | |
|---|---|
| matrix layer | 341 |
| icon group | L3 |
| wave spec | `wave28/341-vernier.md` |

## Purpose

inspects and grades finished parts or stones to fine tolerance, issuing a signed certificate for each piece (`files.read`, `files.write`, `journal.query`); the certificate is a Hoard file.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `journal`
- `camera`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | grading certificates | primary | runs |
| FORTRAN | `trebago` | numeric | tolerance maths | primary | runs |
| Ada | `eve` | limits | cut limits | primary | runs |
| APL | `kron` | arrays | facet angle arrays | primary | runs |
| MUMPS | `myra` | records | provenance tree | primary | runs |
| RPG | `-` | report | reports | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | witness | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (RPG): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `cut_ok` (each facet angle and depth inside the declared cut limits) in Ada as Eve + APL as KRON witnessed
- `tolerance` (deviation per measurement, micrometres) in FORTRAN as Trebago + APL as KRON witnessed
- `certificates` (certificate number, piece, grades, never reused) in COBOL as Sutra
- `facets` angle arrays in APL as KRON
- `provenance` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## The manifest contract

```
zxapp 1
id vernier
matrix 341
name Vernier
needs fs.read
needs fs.write
needs journal
needs camera
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

