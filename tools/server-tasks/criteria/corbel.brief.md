# corbel — the complete brief

| | |
|---|---|
| matrix layer | 338 |
| icon group | L3 |
| wave spec | `wave28/338-corbel.md` |

## Purpose

guides visitors through heritage sites: stories, audio tours, conservation records and ticketing (`place.list`, `files.read`, `files.write`, `hall.load`, `hall.play`).

## Capabilities (the exact registry names for `needs`)

- `location`
- `fs.read`
- `fs.write`
- `peers`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | ticket revenue | primary | runs |
| FORTRAN | `trebago` | numeric | visitor wear model | primary | runs |
| Ada | `eve` | limits | visitor capacity limits | primary | runs |
| APL | `kron` | arrays | site grid | primary | runs |
| MUMPS | `myra` | records | artefact hierarchy | primary | runs |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |
| Pascal | `pascal` | stepwise | step-by-step teaching | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Mojo): see docs/LANGUAGE_CATALOGUE.md for this tier
- **pascal** (Pascal): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `capacity_ok` (visitors in a room at most its declared capacity) in Ada as Eve + APL as KRON witnessed
- `wear` (footfall x the floor's declared wear rate) in FORTRAN as Trebago + APL as KRON witnessed
- `ticket_revenue` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `site` grid in APL as KRON
- `artefacts` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id corbel
matrix 338
name Corbel
needs location
needs fs.read
needs fs.write
needs peers
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

