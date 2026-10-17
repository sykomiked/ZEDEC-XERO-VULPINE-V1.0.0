# gauze — the complete brief

| | |
|---|---|
| matrix layer | 353 |
| icon group | L3 |
| wave spec | `wave29/353-gauze.md` |

## Purpose

Runs a clinic or field medic post: triage, charts, medicines given and handover notes in the Hoard (`files.read`, `files.write`), the patient's medical ID, location (`place.here`), calls on a person's request (`call.start`). Vital-sign devices have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `location`
- `peers`
- `mic`
- `sys.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | billing | primary | runs |
| FORTRAN | `trebago` | numeric | dosage curves | primary | runs |
| Ada | `eve` | limits | hard dose limits | primary | runs |
| APL | `kron` | arrays | vitals arrays | primary | runs |
| MUMPS | `myra` | records | patient record tree | primary | runs |
| AWK | `-` | text | text | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (AWK): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `give_ok`: (critical) "may give this dose now" (Apothecary's signature present AND within the hard dose limit AND no allergy reported, from any source) in Ada as Eve + APL as KRON + FORTRAN as Trebago witnessed (three witnesses)
- - `dose_curve`: (critical) dosage curves (dose per kg by age band from the clinic's own table, a reduced rational) in FORTRAN as Trebago + APL as KRON witnessed
- - `vitals`: vitals arrays (patients x readings by time) in APL as KRON
- - `handover`: handover counts (patients per state, open tasks) in APL as KRON
- - `billing`: billing (codes and totals in minor units, refusing on overflow) in COBOL as Sutra
- - `charts`: patient record tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the medical ID lists no allergy and a carer reports a penicillin reaction: the allergy is BOTH
- the test asserts state 3, the warning flagged, and `give_ok` refusing penicillin until a clinician resolves it. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id gauze
matrix 353
name Gauze
needs fs.read
needs fs.write
needs location
needs peers
needs mic
needs sys.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

