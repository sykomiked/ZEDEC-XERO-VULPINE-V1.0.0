# caliper — the complete brief

| | |
|---|---|
| matrix layer | 177 |
| icon group | L2 |
| wave spec | `wave14/177-caliper.md` |

## Purpose

measures parts and structures and checks them against design tolerances with exact integer maths, records in the Hoard (`files.read`, `files.write`). zxvd has no method for instrument readings input: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | inspection records | primary | runs |
| FORTRAN | `trebago` | numeric | stress calculation | primary | runs |
| Ada | `eve` | limits | tolerance limits | primary | runs |
| APL | `kron` | arrays | measurement arrays | primary | runs |
| MUMPS | `myra` | records | assembly tree | primary | runs |
| Julia | `-` | matrix | matrices and images | primary | planned: needs Lingua front-end |
| Oberon | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Julia): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "within tolerance" (|measured - nominal| at most the tolerance, micrometres) in Ada as Eve + APL as KRON witnessed
- stress (force / area, a reduced rational in kPa) in FORTRAN as Trebago + APL as KRON witnessed
- inspection records (record number, part, pass and fail counts, refused on overflow) in COBOL as Sutra
- measurement arrays in APL as KRON
- assembly tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id caliper
matrix 177
name Caliper
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

