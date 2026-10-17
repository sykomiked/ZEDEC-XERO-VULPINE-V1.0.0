# nymphaeum — the complete brief

| | |
|---|---|
| matrix layer | 334 |
| icon group | L3 |
| wave spec | `wave28/334-nymphaeum.md` |

## Purpose

tests household drinking water and filters, warning when quality drops and logging every result (`files.read`, `files.write`, `notes.list`, `journal.query`). zxvd has no method for water test device readings: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `notify`
- `journal`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | testing costs | primary | runs |
| FORTRAN | `trebago` | numeric | contaminant model | primary | runs |
| Ada | `eve` | limits | safe level limits | primary | runs |
| APL | `kron` | arrays | sample grid | primary | runs |
| MUMPS | `myra` | records | tap and source tree | primary | runs |
| Natural | `-` | query | queries | primary | planned: needs Lingua front-end |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| Icon | `-` | pattern | patterns | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Natural): see docs/LANGUAGE_CATALOGUE.md for this tier
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `safe_ok` (every measured contaminant at most its declared safe level) in Ada as Eve + APL as KRON witnessed
- `contaminant` (trend per contaminant, integer) in FORTRAN as Trebago + APL as KRON witnessed
- `testing_costs` (in minor units) in COBOL as Sutra
- `samples` grid in APL as KRON
- `taps_sources` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id nymphaeum
matrix 334
name Nymphaeum
needs fs.read
needs fs.write
needs notify
needs journal
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

