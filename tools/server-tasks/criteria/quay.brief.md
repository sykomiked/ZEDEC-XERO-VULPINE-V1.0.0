# quay — the complete brief

| | |
|---|---|
| matrix layer | 316 |
| icon group | L3 |
| wave spec | `wave26/316-quay.md` |

## Purpose

runs a port or marina: berths, arrivals, cargo handling, customs papers and fees (`place.list`, `files.read`, `files.write`, `ledger.entries`, `ledger.post`, `print.submit`).

## Capabilities (the exact registry names for `needs`)

- `location`
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
| COBOL | `sutra` | money | harbour dues | primary | runs |
| FORTRAN | `trebago` | numeric | berth scheduling | primary | runs |
| Ada | `eve` | limits | draught and load limits | primary | runs |
| APL | `kron` | arrays | berth grid | primary | runs |
| MUMPS | `myra` | records | vessel and cargo tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- `berth_ok` (vessel draught at most the berth depth at the tide AND length at most the berth) in Ada as Eve + APL as KRON witnessed
- `berth_schedule` (arrivals to berths with no overlap, earliest first) in FORTRAN as Trebago + APL as KRON witnessed
- `harbour_dues` (length x nights x rate in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `berths` grid in APL as KRON
- `vessels_cargo` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id quay
matrix 316
name Quay
needs location
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

