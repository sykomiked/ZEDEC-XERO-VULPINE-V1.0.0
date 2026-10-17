# astrolabe — the complete brief

| | |
|---|---|
| matrix layer | 160 |
| icon group | L2 |
| wave spec | `wave13/160-astrolabe.md` |

## Purpose

plans observing sessions, logs sightings and predicts sky events from local time (`sys.info`) and place (`place.here`), with photos saved to the Hoard (`files.write`). zxvd has no method for a weather source (as Weatherglass reports): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `sys.read`
- `camera`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | observation log | primary | runs |
| FORTRAN | `trebago` | numeric | orbit computation | primary | runs |
| Ada | `eve` | limits | exposure limits | primary | runs |
| APL | `kron` | arrays | star catalogue arrays | primary | runs |
| MUMPS | `myra` | records | object hierarchy | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Modula-2 | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **-** (Simula I): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- next time an object is above the declared altitude inside the night window (minutes of day) in FORTRAN as Trebago + APL as KRON witnessed
- "session go" (cloud at most the limit AND the object up in the window) in Ada as Eve + APL as KRON witnessed
- exposure limits (milliseconds per frame, frames per session) in Ada as Eve
- star catalogue arrays (objects up tonight, by magnitude and altitude) in APL as KRON
- object hierarchy and sightings in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id astrolabe
matrix 160
name Astrolabe
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

