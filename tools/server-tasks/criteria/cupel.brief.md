# cupel — the complete brief

| | |
|---|---|
| matrix layer | 179 |
| icon group | L2 |
| wave spec | `wave14/179-cupel.md` |

## Purpose

simulates atoms, molecules and physical systems on the integer engine and compares models, runs kept in the Hoard (`files.read`, `files.write`), machine facts from `sys.info`.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | run records | primary | runs |
| FORTRAN | `trebago` | numeric | simulation core | primary | runs |
| Ada | `eve` | limits | precision bounds | primary | runs |
| APL | `kron` | arrays | lattice arrays | primary | runs |
| MUMPS | `myra` | records | model tree | primary | runs |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Simula 67): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.

## The spec's Logic clauses (what each component computes)

- one integer leapfrog step (fixed-point positions and velocities, forces from a pair table) in FORTRAN as Trebago + APL as KRON witnessed
- precision bounds (fixed-point scale, at most the declared steps, energy drift at most the bound or the run halts) in Ada as Eve + APL as KRON witnessed
- lattice arrays in APL as KRON
- model tree and run records in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id cupel
matrix 179
name Cupel
needs fs.read
needs fs.write
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

