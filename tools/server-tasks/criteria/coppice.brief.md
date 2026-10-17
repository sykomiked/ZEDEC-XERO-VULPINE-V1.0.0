# coppice — the complete brief

| | |
|---|---|
| matrix layer | 192 |
| icon group | L2 |
| wave spec | `wave15/192-coppice.md` |

## Purpose

manages woodland: tree inventory, growth, felling plans and carbon held, with places (`place.list`, `place.add`) and records in the Hoard (`files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`
- `ledger.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | timber and carbon accounts | primary | runs |
| FORTRAN | `trebago` | numeric | growth model | primary | runs |
| Ada | `eve` | limits | felling limits | primary | runs |
| APL | `kron` | arrays | stand grid | primary | runs |
| MUMPS | `myra` | records | tree inventory | primary | runs |
| Scheme | `scheme` | symbolic | symbolic reasoning | primary | runs |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Modula-2 | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **scheme** (Scheme): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Simula 67): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- carbon held and timber volume (volume x density x factor, kilograms and litres) in COBOL as Sutra + FORTRAN as Trebago witnessed
- diameter growth per year in FORTRAN as Trebago + APL as KRON witnessed
- "felling allowed" (at most the stand's annual growth AND not a protected stand) in Ada as Eve + APL as KRON witnessed
- stand grid in APL as KRON
- tree inventory in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `within_range` | truth | `value min max` | eve | `check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id coppice
matrix 192
name Coppice
needs location
needs fs.read
needs fs.write
needs ledger.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

