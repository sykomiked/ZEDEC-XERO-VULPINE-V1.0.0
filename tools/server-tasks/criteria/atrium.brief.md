# atrium — the complete brief

| | |
|---|---|
| matrix layer | 332 |
| icon group | L3 |
| wave spec | `wave28/332-atrium.md` |

## Purpose

runs a guesthouse or venue: rooms, bookings, catering, housekeeping and guest bills (`files.read`, `files.write`, `ledger.entries`, `ledger.post`, `print.submit`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `print`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | guest folios | primary | runs |
| FORTRAN | `trebago` | numeric | occupancy forecasts | primary | runs |
| Ada | `eve` | limits | capacity limits | primary | runs |
| APL | `kron` | arrays | room grid | primary | runs |
| MUMPS | `myra` | records | guest history | primary | runs |
| System/360 assembler | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| Swift | `swift` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |
| WebAssembly | `wasm` | sandbox | sandboxing | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (System/360 assembler): see docs/LANGUAGE_CATALOGUE.md for this tier
- **swift** (Swift): see docs/LANGUAGE_CATALOGUE.md for this tier
- **wasm** (WebAssembly): the host tier: the witnessed circuit; the same computation compiled to a WebAssembly module the checks can witness.

## The spec's Logic clauses (what each component computes)

- `capacity_ok` (guests per room and per venue at most the declared capacity) in Ada as Eve + APL as KRON witnessed
- `no_double_booking` (a room has at most one booking per night) in APL as KRON + C++ wasm witnessed
- `occupancy` (next month's occupancy from bookings and history) in FORTRAN as Trebago + APL as KRON witnessed
- `folios` (guest bills in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `rooms` grid in APL as KRON
- `guests` history in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id atrium
matrix 332
name Atrium
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs print
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

