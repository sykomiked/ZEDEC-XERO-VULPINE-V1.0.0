# druse — the complete brief

| | |
|---|---|
| matrix layer | 324 |
| icon group | L3 |
| wave spec | `wave27/324-druse.md` |

## Purpose

deep cold storage for things that must outlive you: offline seed copies of data, keys and documents (`backup.run`, `backup.verify`, `files.cross`, `sys.disk`, `recovery.status`).

## Capabilities (the exact registry names for `needs`)

- `backup`
- `fs.read`
- `fs.write`
- `sys.read`
- `peers`
- `journal`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | storage fees | primary | runs |
| FORTRAN | `trebago` | numeric | media decay model | primary | runs |
| Ada | `eve` | limits | access limits | primary | runs |
| APL | `kron` | arrays | shelf grid | primary | runs |
| MUMPS | `myra` | records | holdings tree | primary | runs |
| TRAC | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (TRAC): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `access_ok` (opening a cold copy needs Moraine's release AND the owner or guardians present) in Ada as Eve + APL as KRON witnessed
- `media_decay` (years left per medium from its declared life and age) in FORTRAN as Trebago + APL as KRON witnessed
- `storage_fees` (in minor units) in COBOL as Sutra
- `shelf` grid in APL as KRON
- `holdings` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id druse
matrix 324
name Druse
needs backup
needs fs.read
needs fs.write
needs sys.read
needs peers
needs journal
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

