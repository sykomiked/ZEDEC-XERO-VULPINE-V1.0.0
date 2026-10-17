# flue — the complete brief

| | |
|---|---|
| matrix layer | 189 |
| icon group | L2 |
| wave spec | `wave15/189-flue.md` |

## Purpose

syncs spaces, files and backups across your own devices and your cloud top (`spaces.list`, `files.list`, `files.stat`, `files.cross`, `backup.run`, `backup.list`, `devmesh.devices`, `aipi.put`, `aipi.get`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `backup`
- `peers`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | sync ledger | primary | runs |
| FORTRAN | `trebago` | numeric | bandwidth model | primary | runs |
| Ada | `eve` | limits | quota limits | primary | runs |
| APL | `kron` | arrays | version vectors | primary | runs |
| MUMPS | `myra` | records | folder tree | primary | runs |
| Elixir | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| System/360 assembler | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| TypeScript | `typescript` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier
- **typescript** (TypeScript): the host tier: the browser surface, typed.

## The spec's Logic clauses (what each component computes)

- version vector compare (dominates, equal or concurrent) in APL as KRON + FORTRAN as Trebago witnessed
- "sync fits" (bytes at most the target's free bytes and quota) in Ada as Eve + APL as KRON witnessed
- sync ledger (per file, per device, versions strictly rising, refused on reuse or overflow) in COBOL as Sutra
- seconds to send (bytes / rate) exact in FORTRAN as Trebago
- folder tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id flue
matrix 189
name Flue
needs fs.read
needs fs.write
needs backup
needs peers
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

