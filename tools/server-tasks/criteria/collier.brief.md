# collier — the complete brief

| | |
|---|---|
| matrix layer | 163 |
| icon group | L2 |
| wave spec | `wave13/163-collier.md` |

## Purpose

digs through datasets and files in the Hoard (`search.query`, `files.list`, `files.read`, `files.write`) to extract, clean and label the useful records.

## Capabilities (the exact registry names for `needs`)

- `search`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | provenance ledger | primary | runs |
| FORTRAN | `trebago` | numeric | statistics | primary | runs |
| Ada | `eve` | limits | privacy limits | primary | runs |
| APL | `kron` | arrays | column arrays | primary | runs |
| MUMPS | `myra` | records | source tree | primary | runs |
| SQL | `-` | query | queries | primary | planned: needs Lingua front-end |
| PostScript | `-` | layout | page layout | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (SQL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- provenance ledger (each extracted record's source id and offset, numbered without reuse) as fixed records in COBOL as Sutra
- column statistics (count, min, max, mean floor) exact in FORTRAN as Trebago
- "may this column leave extraction" (not marked personal, or the owner's consent recorded) in Ada as Eve + APL as KRON witnessed
- cleaning over column arrays (duplicates by key, values outside a column's range) in APL as KRON
- source tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id collier
matrix 163
name Collier
needs search
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

