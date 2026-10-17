# escapement — the complete brief

| | |
|---|---|
| matrix layer | 172 |
| icon group | L2 |
| wave spec | `wave14/172-escapement.md` |

## Purpose

keeps exact time across devices (`sys.info`, `devmesh.devices`, `net.peers`) and runs shifts, timesheets and timed jobs from it. zxvd has no method for a peer clock-offset query: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `peers`
- `notify`
- `fs.read`
- `fs.write`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | timesheets | primary | runs |
| FORTRAN | `trebago` | numeric | drift correction | primary | runs |
| Ada | `eve` | limits | deadline limits | primary | runs |
| APL | `kron` | arrays | schedule matrix | primary | runs |
| MUMPS | `myra` | records | shift tree | primary | runs |
| Go | `go` | concurrency | concurrency | primary | planned: SDK host only, no in-app runtime yet |
| Bourne shell | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| Crystal | `-` | machine | low-level system | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Bourne shell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- clock offset (median of the peers' offsets in ms) in FORTRAN as Trebago + APL as KRON witnessed
- "job may run" (inside its window, deadline not passed) in Ada as Eve + APL as KRON witnessed
- timesheets (minutes per shift per person, weekly sums refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- schedule matrix (person by shift, nobody booked twice at once) in APL as KRON
- shift tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |

## The manifest contract

```
zxapp 1
id escapement
matrix 172
name Escapement
needs sys.read
needs peers
needs notify
needs fs.read
needs fs.write
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

