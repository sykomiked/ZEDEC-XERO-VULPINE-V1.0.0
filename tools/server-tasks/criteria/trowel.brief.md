# trowel — the complete brief

| | |
|---|---|
| matrix layer | 184 |
| icon group | L2 |
| wave spec | `wave15/184-trowel.md` |

## Purpose

logs building upkeep: repairs, inspections, contractors and costs, in the Hoard (`files.list`, `files.read`, `files.write`, `sys.disk`) and the books (`ledger.entries`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `ledger.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | repair invoices | primary | runs |
| FORTRAN | `trebago` | numeric | decay model | primary | runs |
| Ada | `eve` | limits | safety thresholds | primary | runs |
| APL | `kron` | arrays | room grid | primary | runs |
| MUMPS | `myra` | records | property tree | primary | runs |
| Erlang | `-` | faults | fault handling | primary | planned: needs Lingua front-end |
| JCL | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "safety check overdue" (days since the last gas, electrical or alarm inspection over the declared interval) in Ada as Eve + APL as KRON witnessed
- repair invoices per item and per year in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed
- remaining life (expected life minus age, months) exact in FORTRAN as Trebago
- room grid in APL as KRON
- property tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## The manifest contract

```
zxapp 1
id trowel
matrix 184
name Trowel
needs fs.read
needs fs.write
needs sys.read
needs ledger.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

