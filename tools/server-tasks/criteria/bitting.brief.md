# bitting — the complete brief

| | |
|---|---|
| matrix layer | 307 |
| icon group | L3 |
| wave spec | `wave25/307-bitting.md` |

## Purpose

issues and revokes digital keys for doors, rooms, vehicles and shared equipment, with a full access history (`passkeys.list`, `passkeys.revoke`, `devmesh.devices`, `devmesh.revoke`, `journal.query`). zxvd has no method for door and vehicle lock control: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `peers`
- `journal`
- `notify`
- `ledger.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | key deposits | primary | runs |
| FORTRAN | `trebago` | numeric | access pattern scoring | primary | runs |
| Ada | `eve` | limits | time-window limits | primary | runs |
| APL | `kron` | arrays | door permission grid | primary | runs |
| MUMPS | `myra` | records | holder tree | primary | runs |
| Ladder logic | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| TRAC | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Ladder logic): see docs/LANGUAGE_CATALOGUE.md for this tier
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `window_ok` (a key opens only inside its time window) in Ada as Eve + APL as KRON witnessed
- `access_pattern` (uses per hour against the holder's usual pattern, integer score) in FORTRAN as Trebago + APL as KRON witnessed
- `deposits` (key deposits in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `doors` permission grid in APL as KRON
- `holders` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id bitting
matrix 307
name Bitting
needs sys.read
needs peers
needs journal
needs notify
needs ledger.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

