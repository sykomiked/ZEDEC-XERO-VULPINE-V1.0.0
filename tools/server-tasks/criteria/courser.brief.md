# courser — the complete brief

| | |
|---|---|
| matrix layer | 188 |
| icon group | L2 |
| wave spec | `wave15/188-courser.md` |

## Purpose

plans and times workouts with music and logs results to the health record in the Hoard (`files.read`, `files.write`), time from `sys.info`.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | membership records | primary | runs |
| FORTRAN | `trebago` | numeric | pace analysis | primary | runs |
| Ada | `eve` | limits | heart-rate limits | primary | runs |
| APL | `kron` | arrays | interval grid | primary | runs |
| MUMPS | `myra` | records | athlete tree | primary | runs |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |
| Delphi | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Simula I): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- pace (seconds per km from time and distance, floor) in FORTRAN as Trebago + APL as KRON witnessed
- "over the heart-rate limit" (above the declared maximum) in Ada as Eve + APL as KRON witnessed, shown as a suggestion to ease off
- interval grid in APL as KRON
- athlete tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id courser
matrix 188
name Courser
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

