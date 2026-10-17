# glimpse — the complete brief

| | |
|---|---|
| matrix layer | 111 |
| icon group | L1 |
| wave spec | `wave9/111-glimpse.md` |

## Purpose

capture the screen, a window or a region, record the screen; save into the Hoard with `files.write` ("Screenshot N.png", first free N from `files.list`); cut recordings with `stream.plan`. zxvd has no display-capture method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| Visual Basic | `dotnet` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |
| dBase | `-` | records | records | primary | planned: needs Lingua front-end |
| ALGOL 68 | `-` | formula | formulas | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **dotnet** (Visual Basic): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (dBase): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- capture region clamped to the screen (x, y, w, h inside width and height, never negative) in kron + trebago witnessed
- recording size (w x h x bytes per pixel x frames, at most 7200 s and 60 fps) exact, refused past the free space `sys.disk` reports, in trebago + kron witnessed
- first free N from the used numbers vector in kron
- A recording always shows that it is recording
- nothing is captured in tests

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id glimpse
matrix 111
name Glimpse
needs fs.read
needs fs.write
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

