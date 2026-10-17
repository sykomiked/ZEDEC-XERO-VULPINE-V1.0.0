# poultice — the complete brief

| | |
|---|---|
| matrix layer | 148 |
| icon group | L2 |
| wave spec | `wave12/148-poultice.md` |

## Purpose

a care plan of symptoms, medicines and vitals kept in the Hoard (`files.read`, `files.write`), reminding carers through notices (`notes.list`, `notes.mark_read`) and keeping doses within the limits the plan declares. zxvd has no method for a notice-post method (as Sundial reports), vitals device readings: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | care costs | primary | runs |
| FORTRAN | `trebago` | numeric | vital trends | primary | runs |
| Ada | `eve` | limits | dosage limits | primary | runs |
| APL | `kron` | arrays | symptom grid | primary | runs |
| MUMPS | `myra` | records | patient tree | primary | runs |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Modula-2 | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Simula I): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "dose within the declared limit" = dose at most the per-dose maximum AND doses in 24 h at most the daily maximum AND time since the last dose at least the minimum interval, in Ada as Eve + APL as KRON + FORTRAN as Trebago witnessed (three witnesses)
- care costs per item and per month in minor units, refused on overflow, in COBOL as Sutra + FORTRAN as Trebago witnessed
- vital trends (7-day min, max, mean floor per vital) in FORTRAN as Trebago
- symptom grid (symptom by day counts) in APL as KRON
- patient tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id poultice
matrix 148
name Poultice
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

