# kiln — the complete brief

| | |
|---|---|
| matrix layer | 186 |
| icon group | L2 |
| wave spec | `wave15/186-kiln.md` |

## Purpose

queues 3D models from the Hoard (`files.read`) for printing, firing or fabrication (`print.status`, `print.submit`, `print.job`, `print.cancel`) and tracks each piece. zxvd has no method for 3D printer and kiln control: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `print`
- `fs.read`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | job costing | primary | runs |
| FORTRAN | `trebago` | numeric | heat curves | primary | runs |
| Ada | `eve` | limits | temperature limits | primary | runs |
| APL | `kron` | arrays | layer arrays | primary | runs |
| MUMPS | `myra` | records | batch tree | primary | runs |
| APT | `-` | toolpath | tool paths | primary | planned: needs Lingua front-end |
| MATLAB | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| Bash | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (APT): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- firing schedule temperature at each step (ramp in tenths of a degree an hour) in FORTRAN as Trebago + APL as KRON witnessed
- "schedule allowed" (peak at most the kiln's maximum AND ramp at most the material's) in Ada as Eve + APL as KRON witnessed
- job costing (material grams x price plus energy, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- layer arrays (layer count, area per layer) in APL as KRON
- batch tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id kiln
matrix 186
name Kiln
needs print
needs fs.read
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

