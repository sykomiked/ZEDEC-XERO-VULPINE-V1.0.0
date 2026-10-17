# lumen — the complete brief

| | |
|---|---|
| matrix layer | 300 |
| icon group | L3 |
| wave spec | `wave25/300-lumen.md` |

## Purpose

a small clinic: appointments, patient records, consultations, referrals and billing in one private system (`files.read`, `files.write`, `notes.list`); bills are kept as Hoard files.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `notify`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | clinic billing | primary | runs |
| FORTRAN | `trebago` | numeric | dosing calculations | primary | runs |
| Ada | `eve` | limits | clinical safety limits | primary | runs |
| APL | `kron` | arrays | lab result grid | primary | runs |
| MUMPS | `myra` | records | patient record tree | primary | runs |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.

## The spec's Logic clauses (what each component computes)

- `clinical_ok` (an action within the clinician's declared rights AND no allergy recorded AND dose within limits) in Ada as Eve + APL as KRON + FORTRAN as Trebago witnessed (three witnesses)
- `dosing` (dose by weight, reduced rational, for display) in FORTRAN as Trebago + APL as KRON witnessed
- `clinic_billing` (codes in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `labs` grid in APL as KRON
- `patients` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `largest_remainder` | num | `pool w1 w2 w3 w4` | sutra | `check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id lumen
matrix 300
name Lumen
needs fs.read
needs fs.write
needs notify
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

