# athenaeum — the complete brief

| | |
|---|---|
| matrix layer | 339 |
| icon group | L3 |
| wave spec | `wave28/339-athenaeum.md` |

## Purpose

a community or national library: catalogue, lending, digital collections and reading rooms (`search.query`, `files.read`, `files.write`, `print.submit`).

## Capabilities (the exact registry names for `needs`)

- `search`
- `fs.read`
- `fs.write`
- `print`
- `ai.ask`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | lending fees and fines | primary | runs |
| FORTRAN | `trebago` | numeric | demand forecasts | primary | runs |
| Ada | `eve` | limits | loan limits | primary | runs |
| APL | `kron` | arrays | catalogue arrays | primary | runs |
| MUMPS | `myra` | records | collection hierarchy | primary | runs |
| Java | `java` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |
| BASIC | `basic` | stepwise | step-by-step teaching | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **java** (Java): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.
- **basic** (BASIC): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `loan_ok` (Bindery's "may lend" AND the reading-room seat free for on-site items) in Ada as Eve + APL as KRON witnessed
- `demand` (holds per title, next quarter) in FORTRAN as Trebago + APL as KRON witnessed
- `fees_fines` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `catalogue` arrays in APL as KRON
- reading-room steps for new readers in BASIC (twin Hatchling)
- `collections` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id athenaeum
matrix 339
name Athenaeum
needs search
needs fs.read
needs fs.write
needs print
needs ai.ask
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

