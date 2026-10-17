# typecase — the complete brief

| | |
|---|---|
| matrix layer | 113 |
| icon group | L1 |
| wave spec | `wave2/113-typecase.md` |

## Purpose

preview, install and check fonts kept in the Hoard (`files.list`, `files.read`, `files.write`), and which scripts each covers. zxvd has no font-registry method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Groovy | `-` | scripting | scripting | primary | planned: needs Lingua front-end |
| Racket | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **-** (Groovy): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- font accepted (each table's offset + length within the file, at most 256 tables, file at most 32 MiB) in kron + trebago witnessed
- script coverage x100 (covered code points / script code points) in kron + trebago witnessed

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |

## The manifest contract

```
zxapp 1
id typecase
matrix 113
name Typecase
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

