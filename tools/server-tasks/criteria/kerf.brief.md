# kerf — the complete brief

| | |
|---|---|
| matrix layer | 290 |
| icon group | L3 |
| wave spec | `wave24/290-kerf.md` |

## Purpose

runs a small workshop or factory: job orders, materials, machine time, quality checks and shipping, from the Hoard (`files.read`, `files.write`), the books (`ledger.entries`, `ledger.post`) and printed papers (`print.submit`). zxvd has no method for machine state readings: use only existing methods; list what's missing in your report.

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
| COBOL | `sutra` | money | job costing | primary | runs |
| FORTRAN | `trebago` | numeric | tolerance calculations | primary | runs |
| Ada | `eve` | limits | machine safety limits | primary | runs |
| APL | `kron` | arrays | production schedule grid | primary | runs |
| MUMPS | `myra` | records | bill-of-materials tree | primary | runs |
| Bourne shell | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Bourne shell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `machine_ok` (a job starts on a machine only with its guards closed, its service not overdue and an operator signed in) in Ada as Eve + APL as KRON witnessed
- `tolerance_stack` (sum of the parts' tolerances along an assembly) in FORTRAN as Trebago + APL as KRON witnessed
- `job_costing` (materials, machine minutes and labour in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `schedule` grid (machine by hour) in APL as KRON
- `bom` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id kerf
matrix 290
name Kerf
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

