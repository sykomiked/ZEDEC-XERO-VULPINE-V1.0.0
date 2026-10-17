# lozenge — the complete brief

| | |
|---|---|
| matrix layer | 281 |
| icon group | L3 |
| wave spec | `wave23/281-lozenge.md` |

## Purpose

tracks hives, pollination contracts with growers and honey sales for beekeepers and their co-op (`ledger.entries`, `ledger.post`, `net.offers`, `files.read`, `files.write`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `net`
- `peers`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | pollination invoices | primary | runs |
| FORTRAN | `trebago` | numeric | colony strength model | primary | runs |
| Ada | `eve` | limits | pesticide distance limits | primary | runs |
| APL | `kron` | arrays | hive grid | primary | runs |
| MUMPS | `myra` | records | colony lineage tree | primary | runs |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Eiffel): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `placement_ok` (a hive at least the declared distance from any field sprayed in the last n days) in Ada as Eve + APL as KRON witnessed
- `colony_strength` (frames of bees and brood, weekly trend) in FORTRAN as Trebago + APL as KRON witnessed
- `pollination_invoice` (hives x days x rate in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `hives` grid in APL as KRON
- `lineage` tree (queen lines) in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id lozenge
matrix 281
name Lozenge
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs net
needs peers
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

