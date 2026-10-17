# crowsnest — the complete brief

| | |
|---|---|
| matrix layer | 358 |
| icon group | L3 |
| wave spec | `wave30/358-crowsnest.md` |

## Purpose

Watches cameras and sensors across sites, spots unusual activity and logs signed evidence: events (`log.tail`, `journal.query`, `journal.verify`), files (`files.stat`, `search.query`), places (`place.list`), the perimeter (`features.get`, `sys.net`). Camera streams from other sites have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `journal`
- `sys.read`
- `search`
- `fs.read`
- `location`
- `camera`
- `net`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | incident billing | primary | runs |
| FORTRAN | `trebago` | numeric | motion detection | primary | runs |
| Ada | `eve` | limits | privacy masking limits | primary | runs |
| APL | `kron` | arrays | frame arrays | primary | runs |
| MUMPS | `myra` | records | site tree | primary | runs |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |
| JOVIAL | `-` | control | machine control | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Haskell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `mask_ok`: (critical) privacy masking (frames outside the owner's property and every masked zone are blanked before storage) in Ada as Eve + APL as KRON witnessed
- - `motion`: (critical) motion detection (changed pixels per block over the threshold, integer frame differences) in FORTRAN as Trebago + APL as KRON witnessed
- - `escalate`: escalation (two sensors agree, or BOTH, goes to a person) in Ada as Eve
- - `frames`: frame arrays in APL as KRON
- - `billing`: incident billing (callouts in minor units, refusing on overflow) in COBOL as Sutra
- - `sites`: site tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the door sensor reports entry and the camera shows no motion: the event is BOTH
- the test asserts state 3 and that it is logged and escalated to a person, not dismissed or alarmed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `flags_none` | truth | `bits forbidden` | kron | `check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id crowsnest
matrix 358
name Crowsnest
needs journal
needs sys.read
needs search
needs fs.read
needs location
needs camera
needs net
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

