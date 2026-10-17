# ravelin — the complete brief

| | |
|---|---|
| matrix layer | 333 |
| icon group | L3 |
| wave spec | `wave28/333-ravelin.md` |

## Purpose

protects critical sites such as power, water and data halls: physical access, cameras, alarms and continuity plans (`journal.query`, `log.tail`, `sys.info`, `features.get`, `files.read`, `files.write`). zxvd has no method for access control, camera and plant sensor feeds: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `journal`
- `sys.read`
- `notify`
- `fs.read`
- `fs.write`
- `net`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | incident costs | primary | runs |
| FORTRAN | `trebago` | numeric | failure models | primary | runs |
| Ada | `eve` | limits | safety interlocks | primary | runs |
| APL | `kron` | arrays | zone sensor grid | primary | runs |
| MUMPS | `myra` | records | site asset tree | primary | runs |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |
| Delphi | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Simula I): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `interlock_ok` (a door, breaker or valve action allowed only with the site's interlocks satisfied and two people approving) in Ada as Eve + APL as KRON witnessed
- `failure_model` (hours of supply left on backup from load and stored energy) in FORTRAN as Trebago + APL as KRON witnessed
- `incident_costs` (in minor units) in COBOL as Sutra
- `zones` sensor grid in APL as KRON
- `site_assets` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id ravelin
matrix 333
name Ravelin
needs journal
needs sys.read
needs notify
needs fs.read
needs fs.write
needs net
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

