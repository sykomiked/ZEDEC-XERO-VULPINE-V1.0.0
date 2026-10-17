# lectern — the complete brief

| | |
|---|---|
| matrix layer | 151 |
| icon group | L2 |
| wave spec | `wave12/151-lectern.md` |

## Purpose

reads documents from the Hoard (`files.read`, `files.write`, `search.query`) with annotations, citations and flashcards drawn straight from what you highlight.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | citation register | primary | runs |
| FORTRAN | `trebago` | numeric | reading-time stats | primary | runs |
| Ada | `eve` | limits | source limits | primary | runs |
| APL | `kron` | arrays | concept matrix | primary | runs |
| MUMPS | `myra` | records | chapter tree | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Common Lisp | `-` | symbolic | symbolic reasoning | primary | planned: needs Lingua front-end |
| TRAC | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **-** (Common Lisp): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "make a card from this highlight" (offset + length inside the document, length 1..4096 bytes) in Ada as Eve + APL as KRON witnessed
- citation register (citation number, source id, page) as fixed records numbered without reuse in COBOL as Sutra
- reading minutes per document per week exact in FORTRAN as Trebago
- source limits (at most 256 sources a document) in Ada as Eve
- concept matrix (concept by source counts) in APL as KRON
- chapter tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id lectern
matrix 151
name Lectern
needs fs.read
needs fs.write
needs search
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

