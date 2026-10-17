# annulus — the complete brief

| | |
|---|---|
| matrix layer | 285 |
| icon group | L3 |
| wave spec | `wave24/285-annulus.md` |

## Purpose

manages medicines for a household or small pharmacy: prescriptions, doses, refills and interaction warnings in the Hoard (`files.read`, `files.write`, `search.query`), with notices (`notes.list`). zxvd has no method for a reminder notice post (as Sedge reports): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `notify`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | dispensing records | primary | runs |
| FORTRAN | `trebago` | numeric | dose kinetics | primary | runs |
| Ada | `eve` | limits | maximum dose limits | primary | runs |
| APL | `kron` | arrays | interaction grid | primary | runs |
| MUMPS | `myra` | records | patient medication tree | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Groovy | `-` | scripting | scripting | primary | planned: needs Lingua front-end |
| Raku | `-` | parse | parsing | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `dose_ok` (each dose and the day's total at most the prescribed maximum) in Ada as Eve + APL as KRON + FORTRAN as Trebago witnessed (three witnesses)
- `kinetics` (amount remaining after t hours by the declared half-life, integer halving steps) in FORTRAN as Trebago + APL as KRON witnessed
- `dispensing` (dispense number, item, quantity, refused on overflow) in COBOL as Sutra
- `interactions` grid (medicine by medicine) in APL as KRON
- `medications` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id annulus
matrix 285
name Annulus
needs fs.read
needs fs.write
needs search
needs notify
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

