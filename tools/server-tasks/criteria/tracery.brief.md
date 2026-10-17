# tracery — the complete brief

| | |
|---|---|
| matrix layer | 275 |
| icon group | L3 |
| wave spec | `wave23/275-tracery.md` |

## Purpose

records wildlife sightings, herd counts and habitat health across a reserve (`place.here`, `place.list`, `place.add`, `files.read`, `files.write`, `search.query`) for rangers and landowners.

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`
- `search`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | permit fees | primary | runs |
| FORTRAN | `trebago` | numeric | population model | primary | runs |
| Ada | `eve` | limits | cull and quota limits | primary | runs |
| APL | `kron` | arrays | sighting grid | primary | runs |
| MUMPS | `myra` | records | species tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- `cull_ok` (a cull or take at most the quota AND the population estimate above the declared floor) in Ada as Eve + APL as KRON witnessed
- `population` (herd trend from survey counts) in FORTRAN as Trebago + APL as KRON witnessed
- `permit_fee` (permits in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `sightings` grid in APL as KRON
- `species` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id tracery
matrix 275
name Tracery
needs location
needs fs.read
needs fs.write
needs search
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

