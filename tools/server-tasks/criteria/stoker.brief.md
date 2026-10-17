# stoker — the complete brief

| | |
|---|---|
| matrix layer | 196 |
| icon group | L2 |
| wave spec | `wave16/196-stoker.md` |

## Purpose

runs models on the fastest hardware present, GPU first: picks the backend, places weights in video memory, checks each model's signature and licence (`model.info`), and reports speed per watt (`power.status`, `power.top`, `sys.info`, `swarm.state`). zxvd has no method for GPU and backend enumeration, video-memory placement: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `ai.ask`
- `sys.read`
- `swarm`
- `fs.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | model admission log | primary | runs |
| FORTRAN | `trebago` | numeric | integer kernels | primary | runs |
| Ada | `eve` | limits | refuses unsigned or non-commercial weights | primary | runs |
| APL | `kron` | arrays | tensor layouts | primary | runs |
| MUMPS | `myra` | records | model catalogue | primary | runs |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| B | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **-** (B): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "admit this model" (signed AND licence allows commercial use) in Ada as Eve + APL as KRON witnessed, refusing unsigned or non-commercial weights
- tokens per second per watt (tokens x 10^6 / microjoules) in FORTRAN as Trebago + APL as KRON witnessed
- layers placed in video memory (largest prefix of layer bytes that fits the free bytes) in APL as KRON + FORTRAN as Trebago witnessed
- model admission log (model, signature result, licence code) as fixed records in COBOL as Sutra
- model catalogue in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id stoker
matrix 196
name Stoker
needs ai.ask
needs sys.read
needs swarm
needs fs.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

