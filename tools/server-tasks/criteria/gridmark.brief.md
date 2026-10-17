# gridmark — the complete brief

| | |
|---|---|
| matrix layer | 120 |
| icon group | L1 |
| wave spec | `wave10/120-gridmark.md` |

## Purpose

make QR codes and EAN-13 barcodes, save them as images (`files.write`), read them from images in the Hoard (`files.read`) or the camera. zxvd has no image-decode method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `camera`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| OCaml | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | formula | formulas | primary | runs |
| AWK | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **-** (OCaml): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- QR version for a length and error level (capacity table) in kron + trebago witnessed
- EAN-13 check digit in trebago + kron witnessed
- Reed-Solomon error words over GF(256) (log/antilog tables) in kron + trebago witnessed
- mask penalty per mask (four rules over the module grid) in kron
- A read link is shown, never opened without the person's tap

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id gridmark
matrix 120
name Gridmark
needs fs.read
needs fs.write
needs camera
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

