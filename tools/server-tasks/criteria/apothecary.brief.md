# apothecary — the complete brief

| | |
|---|---|
| matrix layer | 180 |
| icon group | L2 |
| wave spec | `wave14/180-apothecary.md` |

## Purpose

keeps clinical records and prescriptions in the Hoard (`files.read`, `files.write`) with signed approvals and medical ID. A clinician signs an approval with a passkey (the passkeys methods, under sys.read, added for this). Anything else missing: use only existing methods; list it in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `notify`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | billing codes | primary | runs |
| FORTRAN | `trebago` | numeric | dose calculation | primary | runs |
| Ada | `eve` | limits | prescribing limits | primary | runs |
| APL | `kron` | arrays | lab result grid | primary | runs |
| MUMPS | `myra` | records | patient record tree | primary | runs |
| Haskell | `-` | proof | proofs of correctness | primary | planned: needs Lingua front-end |
| ML | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |
| Tcl | `-` | scripting | scripting | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Haskell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "may sign this prescription" (a clinician seat with prescribing rights AND dose at most the declared maximum AND no recorded allergy to it) in Ada as Eve + APL as KRON witnessed
- dose for display (dose per kg x weight, a reduced rational) in FORTRAN as Trebago + APL as KRON witnessed
- billing codes (fee per code in minor units, totals) in COBOL as Sutra + FORTRAN as Trebago witnessed
- lab result grid in APL as KRON
- patient record tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id apothecary
matrix 180
name Apothecary
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

