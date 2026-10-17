# paisley — the complete brief

| | |
|---|---|
| matrix layer | 373 |
| icon group | L3 |
| wave spec | `wave31/373-paisley.md` |

## Purpose

Designs symmetric patterns, textiles, tiles and logos with live radial symmetry, then exports to print or loom: designs in the Hoard (`files.read`, `files.write`), printed (`print.status`, `print.submit`, `print.job`), print orders in the books (`ledger.post`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `print`
- `ledger.read`
- `ledger.post`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | print orders | primary | runs |
| FORTRAN | `trebago` | numeric | symmetry maths | primary | runs |
| Ada | `eve` | limits | printer limits | primary | runs |
| APL | `kron` | arrays | pattern arrays | primary | runs |
| MUMPS | `myra` | records | design version tree | primary | runs |
| APT | `-` | toolpath | tool paths | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |
| SNOBOL | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (APT): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- - `print_ok`: (critical) printer limits (size, colours and resolution within the printer's or loom's) in Ada as Eve + APL as KRON witnessed
- - `symmetry`: (critical) radial symmetry (rotate each motif point by 360/n using an integer sine table, mirrored when set) in FORTRAN as Trebago + APL as KRON witnessed
- - `toolpath`: cut and stitch path order over the motif (shortest pen-up travel) in APL as KRON
- - `patterns`: pattern arrays in APL as KRON
- - `orders`: print orders (minor units, refusing on overflow) in COBOL as Sutra
- - `versions`: design version tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two collaborators edit the same motif differently: it is BOTH
- the test asserts state 3 and that both are kept as variants. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id paisley
matrix 373
name Paisley
needs fs.read
needs fs.write
needs print
needs ledger.read
needs ledger.post
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

