# freshet — the complete brief

| | |
|---|---|
| matrix layer | 209 |
| icon group | L2 |
| wave spec | `wave17/209-freshet.md` |

## Purpose

hatches a new app project from a template in the Hoard (`files.mkdir`, `files.write`, `files.read`, `files.list`), with signing, version control and a first build ready. zxvd has no method for build run and signing (as Whetstone and Satchel report): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | licence records | primary | runs |
| FORTRAN | `trebago` | numeric | build timings | primary | runs |
| Ada | `eve` | limits | declared limits per template | primary | runs |
| APL | `kron` | arrays | template matrix | primary | runs |
| MUMPS | `myra` | records | project tree | primary | runs |
| Bourne shell | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| REXX | `-` | scripting | scripting | primary | planned: needs Lingua front-end |
| PostScript | `-` | layout | page layout | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Bourne shell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "within the template's declared limits" (budget, needs only names `zxapp_cap_known` accepts, layer matching the slot) in Ada as Eve + APL as KRON witnessed
- template matrix (template by setting) in APL as KRON
- licence records (project, licence code) as fixed records in COBOL as Sutra
- build timings exact in FORTRAN as Trebago
- project tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id freshet
matrix 209
name Freshet
needs fs.read
needs fs.write
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

