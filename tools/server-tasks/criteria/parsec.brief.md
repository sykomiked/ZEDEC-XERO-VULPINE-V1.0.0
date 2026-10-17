# parsec — the complete brief

| | |
|---|---|
| matrix layer | 311 |
| icon group | L3 |
| wave spec | `wave26/311-parsec.md` |

## Purpose

controls a telescope for deep-sky imaging and shares calibrated images with citizen science projects (`place.here`, `sys.info`, `files.write`). zxvd has no method for mount control, and sharing to citizen science projects (none of the parts holds net): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `sys.read`
- `camera`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | observing time accounts | primary | runs |
| FORTRAN | `trebago` | numeric | image stacking maths | primary | runs |
| Ada | `eve` | limits | mount slew limits | primary | runs |
| APL | `kron` | arrays | pixel arrays | primary | runs |
| MUMPS | `myra` | records | target catalogue | primary | runs |
| MATLAB | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Modula-2 | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (MATLAB): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `slew_ok` (mount target above the horizon limit AND slew speed at most the mount's declared maximum) in Ada as Eve + APL as KRON witnessed
- `stack` (frames aligned and summed as integers, then divided by count, floor) in FORTRAN as Trebago + APL as KRON witnessed
- `time_accounts` (observing hours per member) in COBOL as Sutra
- `pixels` arrays in APL as KRON
- `targets` catalogue in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## The manifest contract

```
zxapp 1
id parsec
matrix 311
name Parsec
needs location
needs sys.read
needs camera
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

