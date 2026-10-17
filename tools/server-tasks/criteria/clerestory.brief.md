# clerestory — the complete brief

| | |
|---|---|
| matrix layer | 293 |
| icon group | L3 |
| wave spec | `wave24/293-clerestory.md` |

## Purpose

supports a research project end to end: reading lists, notes, citations, drafts and supervisor feedback in the Hoard (`files.read`, `files.write`, `search.query`), grant spending kept as a Hoard file.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | grant spending | primary | runs |
| FORTRAN | `trebago` | numeric | statistics engine | primary | runs |
| Ada | `eve` | limits | ethics limits | primary | runs |
| APL | `kron` | arrays | literature matrix | primary | runs |
| MUMPS | `myra` | records | chapter hierarchy | primary | runs |
| Mathematica | `-` | symbolic | symbolic reasoning | primary | planned: needs Lingua front-end |
| Natural | `-` | query | queries | primary | planned: needs Lingua front-end |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Mathematica): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `ethics_ok` (data with people in it is used only under an approval on file and within its expiry) in Ada as Eve + APL as KRON witnessed
- `stats` (mean floor, median and counts over the study's integer data) in FORTRAN as Trebago + APL as KRON witnessed
- `grant_spend` (spend against the grant's lines, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `literature` matrix in APL as KRON
- `chapters` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id clerestory
matrix 293
name Clerestory
needs fs.read
needs fs.write
needs search
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

