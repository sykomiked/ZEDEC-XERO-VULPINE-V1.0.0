# pinhole — the complete brief

| | |
|---|---|
| matrix layer | 58 |
| icon group | L1 |
| wave spec | `wave7/058-pinhole.md` |

## Purpose

photos saved in the Hoard (`files.write`), room left from `sys.disk`, a place tag only after the owner's answer (`place.ask`). Capturing from the camera has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `camera`
- `location`
- `fs.write`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| APL | `kron` | arrays | arrays | primary | runs |
| FORTRAN | `trebago` | numeric | exact numerics | primary | runs |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |
| Julia | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **-** (LabVIEW): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- device ready (sensor present, mode supported, buffer fits, as flags) in forth + eve witnessed
- "photo fits" (bytes vs free) in sutra + kron witnessed
- bytes per album exact in trebago
- exposure limits in eve
- photo record in myra
- The camera is shown as on whenever it is
- the location is never added without the owner's permit

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id pinhole
matrix 58
name Pinhole
needs camera
needs location
needs fs.write
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

