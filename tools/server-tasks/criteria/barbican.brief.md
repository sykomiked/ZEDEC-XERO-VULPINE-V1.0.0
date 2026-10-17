# barbican — the complete brief

| | |
|---|---|
| matrix layer | 272 |
| icon group | L3 |
| wave spec | `wave23/272-barbican.md` |

## Purpose

guards one person's devices, logins and identity keys (`devmesh.devices`, `passkeys.list`, `seats.list`), showing every intrusion attempt from the journal (`journal.query`, `log.tail`) and blocking it in plain words.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `peers`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | breach cost log | primary | runs |
| FORTRAN | `trebago` | numeric | anomaly scores | primary | runs |
| Ada | `eve` | limits | lockout limits | primary | runs |
| APL | `kron` | arrays | device threat grid | primary | runs |
| MUMPS | `myra` | records | identity tree | primary | runs |
| Nim | `-` | machine | low-level system | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Nim): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `lockout_ok` (lock a login out after the declared failed attempts inside the window, unlock only by the owner) in Ada as Eve + APL as KRON witnessed
- `anomaly` (failed attempts per device per hour against the device's 14-day mean, integer score) in FORTRAN as Trebago + APL as KRON witnessed
- `breach_cost` (cost of a breach as declared items in minor units, refused on overflow) in COBOL as Sutra
- `threat_grid` (device by attempt kind) in APL as KRON
- `identity` tree (person, devices, keys) in MUMPS as MYRA
- the plain-words message is chosen in C from the decision codes

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id barbican
matrix 272
name Barbican
needs sys.read
needs peers
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

