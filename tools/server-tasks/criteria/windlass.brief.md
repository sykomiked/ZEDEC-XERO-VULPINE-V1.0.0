# windlass — the complete brief

| | |
|---|---|
| matrix layer | 340 |
| icon group | L3 |
| wave spec | `wave28/340-windlass.md` |

## Purpose

plans, logs and audits a mine or quarry: ore grades, shifts, safety checks and royalty payments (`files.read`, `files.write`, `search.query`, `ledger.entries`, `ledger.post`, `ledger.hold`). zxvd has no method for gas detector readings: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `ledger.read`
- `ledger.post`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | royalty ledger | primary | runs |
| FORTRAN | `trebago` | numeric | ore grade model | primary | runs |
| Ada | `eve` | limits | gas and depth limits | primary | runs |
| APL | `kron` | arrays | assay grid | primary | runs |
| MUMPS | `myra` | records | crew health tree | primary | runs |
| PHP | `php` | report | reports | primary | planned: SDK host only, no in-app runtime yet |
| Occam | `-` | concurrency | concurrency | primary | planned: needs Lingua front-end |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **php** (PHP): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Occam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `gas_depth_ok` (gas readings and working depth inside the declared limits before a shift goes in) in Ada as Eve + APL as KRON witnessed
- `ore_grade` (grade per block from assays) in FORTRAN as Trebago + APL as KRON witnessed
- `royalty_ledger` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `assays` grid in APL as KRON
- `crew_health` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |

## The manifest contract

```
zxapp 1
id windlass
matrix 340
name Windlass
needs fs.read
needs fs.write
needs search
needs ledger.read
needs ledger.post
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

