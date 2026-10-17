# cistern — the complete brief

| | |
|---|---|
| matrix layer | 296 |
| icon group | L3 |
| wave spec | `wave25/296-cistern.md` |

## Purpose

runs a water utility: pipes, pumps, leaks, meter readings, quality tests and customer bills (`files.read`, `files.write`, `journal.query`, `ledger.entries`, `ledger.post`, `print.submit`). zxvd has no method for meter, pump and quality sensor readings: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `journal`
- `ledger.read`
- `ledger.post`
- `print`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | customer billing | primary | runs |
| FORTRAN | `trebago` | numeric | hydraulic model | primary | runs |
| Ada | `eve` | limits | pressure and dosing limits | primary | runs |
| APL | `kron` | arrays | meter arrays | primary | runs |
| MUMPS | `myra` | records | network asset tree | primary | runs |
| JOVIAL | `-` | control | machine control | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (JOVIAL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- `pressure_dose_ok` (pressure inside the declared band AND disinfectant dose inside its band) in Ada as Eve + APL as KRON witnessed
- `hydraulic` (flow and head loss per pipe from the integer tables) in FORTRAN as Trebago + APL as KRON witnessed
- `billing` (cubic metres x tariff bands in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `meters` arrays in APL as KRON
- `network_assets` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |

## The manifest contract

```
zxapp 1
id cistern
matrix 296
name Cistern
needs fs.read
needs fs.write
needs journal
needs ledger.read
needs ledger.post
needs print
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

