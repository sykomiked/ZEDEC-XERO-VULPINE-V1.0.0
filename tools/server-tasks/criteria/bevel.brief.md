# bevel — the complete brief

| | |
|---|---|
| matrix layer | 318 |
| icon group | L3 |
| wave spec | `wave26/318-bevel.md` |

## Purpose

finds and redacts personal data in documents and datasets before sharing, keeping only what is needed (`files.read`, `files.write`, `search.query`, `journal.query`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `journal`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | disclosure log | primary | runs |
| FORTRAN | `trebago` | numeric | re-identification risk | primary | runs |
| Ada | `eve` | limits | retention limits | primary | runs |
| APL | `kron` | arrays | field sensitivity grid | primary | runs |
| MUMPS | `myra` | records | record hierarchy | primary | runs |
| SQL | `-` | query | queries | primary | planned: needs Lingua front-end |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |
| Icon | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (SQL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `redact` (a field is redacted unless every rule marks it public) in Ada as Eve + APL as KRON witnessed
- `reid_risk` (smallest group size sharing the kept fields, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `disclosure_log` (disclosure number, recipient, fields, never reused) in COBOL as Sutra
- `sensitivity` grid (field by rule) in APL as KRON
- `records` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id bevel
matrix 318
name Bevel
needs fs.read
needs fs.write
needs search
needs journal
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

