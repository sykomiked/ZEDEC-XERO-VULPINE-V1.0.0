# stationer — the complete brief

| | |
|---|---|
| matrix layer | 202 |
| icon group | L2 |
| wave spec | `wave16/202-stationer.md` |

## Purpose

designs forms, letters and printed templates in the Hoard (`files.read`, `files.write`), prints (`print.status`, `print.submit`) and scans them (`scan.start`), and folds them into fill-and-sign packets.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `print`
- `scan`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | form registry | primary | runs |
| FORTRAN | `trebago` | numeric | layout math | primary | runs |
| Ada | `eve` | limits | field rules | primary | runs |
| APL | `kron` | arrays | field grid | primary | runs |
| MUMPS | `myra` | records | form tree | primary | runs |
| PostScript | `-` | layout | page layout | primary | planned: needs Lingua front-end |
| APT | `-` | toolpath | tool paths | primary | planned: needs Lingua front-end |
| Lua | `-` | scripting | scripting | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (PostScript): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- field boxes fit the page (twips) in FORTRAN as Trebago + APL as KRON witnessed
- "field valid" (length, range and required as declared) in Ada as Eve + APL as KRON witnessed
- form registry (form number, version, never reused) in COBOL as Sutra
- field grid in APL as KRON
- form tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id stationer
matrix 202
name Stationer
needs fs.read
needs fs.write
needs print
needs scan
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

