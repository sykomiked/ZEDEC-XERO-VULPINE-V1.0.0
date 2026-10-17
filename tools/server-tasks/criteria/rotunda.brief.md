# rotunda — the complete brief

| | |
|---|---|
| matrix layer | 331 |
| icon group | L3 |
| wave spec | `wave27/331-rotunda.md` |

## Purpose

books shared research instruments and facilities: time slots, training, usage logs and cost sharing (`files.read`, `files.write`, `ledger.entries`, `ledger.post`, `sys.info`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | usage charges | primary | runs |
| FORTRAN | `trebago` | numeric | slot optimisation | primary | runs |
| Ada | `eve` | limits | operator certification limits | primary | runs |
| APL | `kron` | arrays | booking grid | primary | runs |
| MUMPS | `myra` | records | facility tree | primary | runs |
| K | `kron` | query | queries | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Scheme | `scheme` | symbolic | symbolic reasoning | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier
- **scheme** (Scheme): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `operator_ok` (a booking only for someone certified on that instrument and not expired) in Ada as Eve + APL as KRON witnessed
- `slots` (fit requests into free slots, earliest first, no overlap) in FORTRAN as Trebago + APL as KRON witnessed
- `usage_charges` (hours x rate shared by group, largest remainder) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `bookings` grid in APL as KRON
- `facilities` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `largest_remainder` | num | `pool w1 w2 w3 w4` | sutra | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |

## The manifest contract

```
zxapp 1
id rotunda
matrix 331
name Rotunda
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

