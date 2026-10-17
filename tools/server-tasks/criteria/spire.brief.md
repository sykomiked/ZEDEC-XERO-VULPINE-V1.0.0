# spire — the complete brief

| | |
|---|---|
| matrix layer | 323 |
| icon group | L3 |
| wave spec | `wave27/323-spire.md` |

## Purpose

manages geology and mining surveys: samples, claims, drill logs, reserves and land restoration (`files.read`, `files.write`, `search.query`, `place.list`, `aipi.execute`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `location`
- `peers`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | royalty accounts | primary | runs |
| FORTRAN | `trebago` | numeric | ore body model | primary | runs |
| Ada | `eve` | limits | extraction limits | primary | runs |
| APL | `kron` | arrays | drill grid | primary | runs |
| MUMPS | `myra` | records | claim hierarchy | primary | runs |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- `extraction_ok` (tonnes at most the permit AND inside the claim's parcel) in Ada as Eve + APL as KRON witnessed
- `ore_body` (tonnage x grade from drill intervals, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `royalties` (royalty accounts in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `drill` grid in APL as KRON
- `claims` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## The manifest contract

```
zxapp 1
id spire
matrix 323
name Spire
needs fs.read
needs fs.write
needs search
needs location
needs peers
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

