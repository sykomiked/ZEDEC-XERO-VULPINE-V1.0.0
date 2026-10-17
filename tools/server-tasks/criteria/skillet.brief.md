# skillet — the complete brief

| | |
|---|---|
| matrix layer | 159 |
| icon group | L2 |
| wave spec | `wave13/159-skillet.md` |

## Purpose

turns meal plans into pantry checks, shopping lists and cooking timers for a household, kept in the Hoard (`files.read`, `files.write`), with spend from the books (`ledger.entries`) and time from `sys.info`. zxvd has no method for timer and notice post (as Sundial reports): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ledger.read`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | grocery spend | primary | runs |
| FORTRAN | `trebago` | numeric | portion scaling | primary | runs |
| Ada | `eve` | limits | allergen limits | primary | runs |
| APL | `kron` | arrays | pantry grid | primary | runs |
| MUMPS | `myra` | records | household tree | primary | runs |
| JCL | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| IPL | `-` | lists | lists | primary | planned: needs Lingua front-end |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (JCL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- shopping list (needed minus in the pantry, never below zero, per item) in APL as KRON + FORTRAN as Trebago witnessed
- "keep off the plate" (any household member excludes one of the ingredient's allergens, over the household) in Ada as Eve + APL as KRON witnessed
- grocery spend per week in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- portions for the household (Kettle's scaling) in FORTRAN as Trebago
- pantry grid in APL as KRON
- household tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## The manifest contract

```
zxapp 1
id skillet
matrix 159
name Skillet
needs fs.read
needs fs.write
needs ledger.read
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

