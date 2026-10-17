# pinion — the complete brief

| | |
|---|---|
| matrix layer | 317 |
| icon group | L3 |
| wave spec | `wave26/317-pinion.md` |

## Purpose

automates and maintains factory equipment: machine states, predictive maintenance, spare parts and downtime (`sys.info`, `files.read`, `files.write`). zxvd has no method for machine sensor readings and control: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `notify`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | maintenance costs | primary | runs |
| FORTRAN | `trebago` | numeric | wear models | primary | runs |
| Ada | `eve` | limits | interlock limits | primary | runs |
| APL | `kron` | arrays | sensor arrays | primary | runs |
| MUMPS | `myra` | records | machine hierarchy | primary | runs |
| Bourne shell | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| REXX | `-` | scripting | scripting | primary | planned: needs Lingua front-end |
| Mojo | `-` | ai | AI | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Bourne shell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `interlock_ok` (a machine runs only with every interlock closed and no safety stop latched) in Ada as Eve + APL as KRON witnessed
- `wear` (remaining hours from load history) in FORTRAN as Trebago + APL as KRON witnessed
- `maintenance_costs` (parts and labour in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `sensors` arrays in APL as KRON
- `machines` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id pinion
matrix 317
name Pinion
needs sys.read
needs notify
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

