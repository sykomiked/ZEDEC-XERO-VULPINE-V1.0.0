# chainman — the complete brief

| | |
|---|---|
| matrix layer | 154 |
| icon group | L2 |
| wave spec | `wave12/154-chainman.md` |

## Purpose

surveys land parcels, boundaries and waypoints (`place.here`, `place.add`, `place.list`) into maps with measured records in the Hoard (`files.read`, `files.write`). zxvd has no method for a general signing method for a survey record (no daemon method signs an arbitrary file): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | title register | primary | runs |
| FORTRAN | `trebago` | numeric | area computation | primary | runs |
| Ada | `eve` | limits | boundary tolerances | primary | runs |
| APL | `kron` | arrays | parcel grid | primary | runs |
| MUMPS | `myra` | records | land hierarchy | primary | runs |
| Haskell | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Haskell): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- parcel area (twice the area by the shoelace rule over corner vectors in millimetres, exact) in FORTRAN as Trebago + APL as KRON witnessed
- "closes within tolerance" (closure error at most the declared millimetres per 100 m) in Ada as Eve + APL as KRON witnessed
- title register (title number, parcel, area) as fixed records refused on overflow in COBOL as Sutra
- parcel grid (cells claimed by each parcel, overlaps found) in APL as KRON
- land hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id chainman
matrix 154
name Chainman
needs location
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

