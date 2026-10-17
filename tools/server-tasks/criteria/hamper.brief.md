# hamper — the complete brief

| | |
|---|---|
| matrix layer | 112 |
| icon group | L1 |
| wave spec | `wave9/112-hamper.md` |

## Purpose

open and make compressed archives in the Hoard (`files.list`, `files.stat`, `files.read`, `files.write`, `files.mkdir`), never over an existing item. zxvd has no archive codec method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| dBase | `-` | records | records | primary | planned: needs Lingua front-end |
| Autocode | `-` | formula | formulas | primary | planned: needs Lingua front-end |
| AWK | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **-** (dBase): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- every entry in bounds (offset + size within the archive length, over integer vectors) in kron + trebago witnessed
- entry path safe (no leading slash, no ".." segment, depth at most 32, as integer counts) in eve + kron witnessed
- expansion guard (unpacked total at most 100 x packed and at most free bytes from `sys.disk`) in trebago + kron witnessed
- totals (bytes, files) exact in trebago
- An archive that fails any check is refused whole
- nothing is written before every check passes

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## The manifest contract

```
zxapp 1
id hamper
matrix 112
name Hamper
needs fs.read
needs fs.write
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

