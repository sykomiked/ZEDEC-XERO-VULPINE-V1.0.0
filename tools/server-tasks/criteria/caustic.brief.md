# caustic — the complete brief

| | |
|---|---|
| matrix layer | 329 |
| icon group | L3 |
| wave spec | `wave27/329-caustic.md` |

## Purpose

surveys and restores coral reefs: dive logs, bleaching records, water quality and restoration plots (`files.read`, `files.write`, `search.query`, `place.list`, `ledger.entries`). zxvd has no method for water temperature and quality readings: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `location`
- `ledger.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | grant accounts | primary | runs |
| FORTRAN | `trebago` | numeric | temperature stress model | primary | runs |
| Ada | `eve` | limits | dive safety limits | primary | runs |
| APL | `kron` | arrays | reef plot grid | primary | runs |
| MUMPS | `myra` | records | species tree | primary | runs |
| Elixir | `-` | faults | fault handling | primary | planned: needs Lingua front-end |
| Logo | `logo` | geometry | geometry | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier
- **logo** (Logo): the run tier (Lingua): turtle/geometry steps for the visual apps.

## The spec's Logic clauses (what each component computes)

- `dive_ok` (depth and time inside the declared no-stop limits, buddy listed) in Ada as Eve + APL as KRON witnessed
- `heat_stress` (degree heating weeks from daily temperatures) in FORTRAN as Trebago + APL as KRON witnessed
- `grant_accounts` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `plots` grid in APL as KRON
- `species` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id caustic
matrix 329
name Caustic
needs fs.read
needs fs.write
needs search
needs location
needs ledger.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

