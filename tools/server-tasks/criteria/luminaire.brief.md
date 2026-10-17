# luminaire — the complete brief

| | |
|---|---|
| matrix layer | 308 |
| icon group | L3 |
| wave spec | `wave26/308-luminaire.md` |

## Purpose

controls lighting across a home or building, saving energy with schedules, presence and daylight (`power.status`, `power.budget`, `sys.info`, `files.read`, `files.write`). zxvd has no method for light and presence device control: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `fs.read`
- `fs.write`
- `notify`
- `location`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | energy bills | primary | runs |
| FORTRAN | `trebago` | numeric | daylight model | primary | runs |
| Ada | `eve` | limits | circuit load limits | primary | runs |
| APL | `kron` | arrays | room light grid | primary | runs |
| MUMPS | `myra` | records | building zone tree | primary | runs |
| Bash | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| IEC 61131-3 Structured Text | `-` | control | machine control | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Bash): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `circuit_ok` (lights on a circuit at most its declared load) in Ada as Eve + APL as KRON witnessed
- `daylight` (daylight minutes and level by hour from the date and place, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `energy_bills` (Wh x tariff in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `rooms` light grid in APL as KRON
- `zones` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## The manifest contract

```
zxapp 1
id luminaire
matrix 308
name Luminaire
needs sys.read
needs fs.read
needs fs.write
needs notify
needs location
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

