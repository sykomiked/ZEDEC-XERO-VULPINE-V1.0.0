# rapids — the complete brief

| | |
|---|---|
| matrix layer | 228 |
| icon group | L2 |
| wave spec | `wave18/228-rapids.md` |

## Purpose

Supports emergency crews in one place: triage and vitals records in the Hoard (`files.read`, `files.write`), the patient's medical ID through Watchfire's records, location (`place.here`, `place.ask`), calls (`call.start`, `call.status`, `call.hangup`, signalling only), crew devices (`phone.list`). Vital-sign devices have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `peers`
- `mic`
- `fs.read`
- `fs.write`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | incident billing | primary | runs |
| FORTRAN | `trebago` | numeric | vitals trends | primary | runs |
| Ada | `eve` | limits | dosage limits | primary | runs |
| APL | `kron` | arrays | triage grid | primary | runs |
| MUMPS | `myra` | records | patient record tree | primary | runs |
| Elixir | `-` | messaging | messaging | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) triage category from integer inputs (walking, breathing, rate per minute, capillary refill ms, obeys commands) in APL as KRON + Ada as Eve witnessed
- shown as a suggestion the crew confirms
- - (critical) dosage limits (dose per kg within the service's own protocol table, entered by the service
- refused above it) in Ada as Eve + APL as KRON witnessed
- - vitals trends (change per 5 minutes, exact) in FORTRAN as Trebago
- - triage grid (patients x category, time seen) in APL as KRON
- - incident billing (minutes and charges in minor units, refusing on overflow) in COBOL as Sutra
- - patient record tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the medical ID says no allergies and a second record says penicillin allergy, giving BOTH
- the test asserts state 3 and that both are shown at the scene and highlighted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id rapids
matrix 228
name Rapids
needs location
needs peers
needs mic
needs fs.read
needs fs.write
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

