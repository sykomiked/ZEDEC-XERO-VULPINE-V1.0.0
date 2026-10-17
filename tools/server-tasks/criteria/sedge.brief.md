# sedge — the complete brief

| | |
|---|---|
| matrix layer | 248 |
| icon group | L2 |
| wave spec | `wave20/248-sedge.md` |

## Purpose

Tracks household medicines (advice only): doses, schedules, stock and interactions kept in the Hoard (`files.read`, `files.write`, `search.query`), notices (`notes.list`). Posting a reminder has no method (as Bowline): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `search`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | stock records | primary | runs |
| FORTRAN | `trebago` | numeric | dose timing | primary | runs |
| Ada | `eve` | limits | maximum dose limits | primary | runs |
| APL | `kron` | arrays | interaction matrix | primary | runs |
| MUMPS | `myra` | records | patient tree | primary | runs |
| Bourne shell | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| PL/I | `-` | business | business records | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Bourne shell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "may take now" (the last 24 hours plus this dose within the maximum typed from the label, and the minimum interval passed) in Ada as Eve + APL as KRON witnessed
- - (critical) next dose time (minutes from the last dose and the interval) in FORTRAN as Trebago + APL as KRON witnessed
- - interaction matrix (medicines x medicines, flags from the entered sources) in APL as KRON
- - stock records (doses left and days of stock, refusing below zero) in COBOL as Sutra
- - patient tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): source A lists no interaction and source B a major one for the same pair, giving BOTH
- the test asserts state 3, both shown, and a prompt to check with a pharmacist. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id sedge
matrix 248
name Sedge
needs fs.read
needs fs.write
needs search
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

