# warding — the complete brief

| | |
|---|---|
| matrix layer | 173 |
| icon group | L2 |
| wave spec | `wave14/173-warding.md` |

## Purpose

issues and revokes keys for doors, devices and accounts from one place (`passkeys.list`, `passkeys.revoke`, `devmesh.devices`, `devmesh.revoke`, `seats.list`). zxvd has no method for door lock control (as Homestead reports): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | key register | primary | runs |
| FORTRAN | `trebago` | numeric | entropy checks | primary | runs |
| Ada | `eve` | limits | expiry limits | primary | runs |
| APL | `kron` | arrays | access grid | primary | runs |
| MUMPS | `myra` | records | holder tree | primary | runs |
| Nim | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| Kotlin | `kotlin` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Nim): see docs/LANGUAGE_CATALOGUE.md for this tier
- **kotlin** (Kotlin): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "key active" = issued AND not revoked on any device AND not expired, in Ada as Eve + APL as KRON witnessed
- expiry limits (1..3650 days) in Ada as Eve
- key register (serial numbers strictly rising, never reused, refused on overflow) in COBOL as Sutra
- entropy check (bits from length x log2 of the alphabet size, integer table, inputs are lengths only) in FORTRAN as Trebago + APL as KRON witnessed
- access grid (holder by door or device) in APL as KRON
- holder tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id warding
matrix 173
name Warding
needs sys.read
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

