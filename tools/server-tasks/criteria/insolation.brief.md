# insolation — the complete brief

| | |
|---|---|
| matrix layer | 335 |
| icon group | L3 |
| wave spec | `wave28/335-insolation.md` |

## Purpose

runs a community wind and solar co-op: generation, storage, shares, mutual-aid grants and grid exports (`power.status`, `ledger.entries`, `ledger.post`, `ledger.hold`, `devmesh.devices`). zxvd has no method for turbine, inverter and meter readings: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `ledger.read`
- `ledger.post`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | member grants | primary | runs |
| FORTRAN | `trebago` | numeric | wind and sun yield model | primary | runs |
| Ada | `eve` | limits | grid export limits | primary | runs |
| APL | `kron` | arrays | turbine and panel grid | primary | runs |
| MUMPS | `myra` | records | member share tree | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `export_ok` (export at most the declared grid allowance AND storage at least its reserve) in Ada as Eve + APL as KRON witnessed
- `generation` (expected Wh from wind and sun hours, integer model) in FORTRAN as Trebago + APL as KRON witnessed
- `member_grants` (each member's grant by shares, largest remainder, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `assets` grid (turbine and panel) in APL as KRON
- `member_shares` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `largest_remainder` | num | `pool w1 w2 w3 w4` | sutra | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |

## The manifest contract

```
zxapp 1
id insolation
matrix 335
name Insolation
needs sys.read
needs ledger.read
needs ledger.post
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

