# equant — the complete brief

| | |
|---|---|
| matrix layer | 320 |
| icon group | L3 |
| wave spec | `wave27/320-equant.md` |

## Purpose

arbitrates disputes: claims, evidence, hearings, rulings and enforcement under a published rulebook (`files.read`, `files.write`, `journal.query`, `web4.verify`, `pspace.post`, `ledger.hold`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `journal`
- `peers`
- `net`
- `ledger.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | settlement amounts | primary | runs |
| FORTRAN | `trebago` | numeric | damages calculation | primary | runs |
| Ada | `eve` | limits | procedure deadlines | primary | runs |
| APL | `kron` | arrays | evidence matrix | primary | runs |
| MUMPS | `myra` | records | case hierarchy | primary | runs |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |
| TeX | `-` | typesetting | typesetting | primary | planned: needs Lingua front-end |
| SNOBOL | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Prolog): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `procedure_ok` (each step inside its deadline under the published rulebook) in Ada as Eve + APL as KRON witnessed
- `damages` (proven losses plus interest at the stated rate, reduced rationals) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `settlement` (agreed amounts in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `evidence` matrix in APL as KRON
- `cases` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `conserved_sum` | truth | `whole a b c d` | trebago | `check conserved_sum whole=100,100,100,101 a=25,0,0,25 b=25,0,0,25 c=25,0,0,25 d=25,100,99,25 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |

## The manifest contract

```
zxapp 1
id equant
matrix 320
name Equant
needs fs.read
needs fs.write
needs journal
needs peers
needs net
needs ledger.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

