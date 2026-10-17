# pavise — the complete brief

| | |
|---|---|
| matrix layer | 153 |
| icon group | L2 |
| wave spec | `wave12/153-pavise.md` |

## Purpose

one security perimeter combining firewall paths, app permissions and keys into a single policy, read from `features.get`, `sys.net`, `net.peers`, `devmesh.devices`, `passkeys.list`, with its history from `journal.query` and `journal.verify`. zxvd has no method for firewall rule write, capability grant write: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `net`
- `peers`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | access ledger | primary | runs |
| FORTRAN | `trebago` | numeric | threat scoring | primary | runs |
| Ada | `eve` | limits | declared policy bounds | primary | runs |
| APL | `kron` | arrays | permission matrix | primary | runs |
| MUMPS | `myra` | records | trust tree | primary | runs |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "allow" = Gatehouse allows AND Hedgerow's path reachable AND the key valid and not expired AND no policy denies, in Ada as Eve + APL as KRON witnessed, joined with `lpres_conjoin` (C) so BOTH is never TRUE
- access ledger (decision number, app, capability) as fixed records numbered without reuse in COBOL as Sutra
- exposure score (open paths, keys expiring within 30 days, unsigned apps, each weighted) exact in FORTRAN as Trebago
- permission matrix (app by capability) in APL as KRON
- declared policy bounds in Ada as Eve
- trust tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| `clamp` | num | `value min max` | kron | `check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100` |

## The manifest contract

```
zxapp 1
id pavise
matrix 153
name Pavise
needs sys.read
needs net
needs peers
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

