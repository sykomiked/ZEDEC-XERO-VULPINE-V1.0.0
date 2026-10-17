# parallax — the complete brief

| | |
|---|---|
| matrix layer | 294 |
| icon group | L3 |
| wave spec | `wave24/294-parallax.md` |

## Purpose

a visual effects studio for layered images, motion and the depth effect, from sketch to finished shot, kept in the Hoard (`files.read`, `files.write`, `sys.disk`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | production budget | primary | runs |
| FORTRAN | `trebago` | numeric | render maths | primary | runs |
| Ada | `eve` | limits | frame budget limits | primary | runs |
| APL | `kron` | arrays | pixel arrays | primary | runs |
| MUMPS | `myra` | records | shot hierarchy | primary | runs |
| MATLAB | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (MATLAB): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `frame_ok` (each frame's render within the declared frame budget in ms and bytes) in Ada as Eve + APL as KRON witnessed
- `render` (layer compositing over integer alpha, clamped 0..255) in FORTRAN as Trebago + APL as KRON witnessed
- `budget` (production budget per shot in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `pixels` arrays in APL as KRON
- `shots` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id parallax
matrix 294
name Parallax
needs fs.read
needs fs.write
needs sys.read
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

