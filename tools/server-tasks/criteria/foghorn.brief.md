# foghorn — the complete brief

| | |
|---|---|
| matrix layer | 400 |
| icon group | L3 |
| wave spec | `wave33/400-foghorn.md` |

## Purpose

Runs navigation aids and harbour signals: hazards broadcast once a person approves (`social.post`, `chat.post`, `pspace.post`, `harmonic.encode`, `ehop.bands`), beacon health (`log.tail`, `journal.query`), places (`place.list`, `place.add`). Beacon and light control has no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `journal`
- `location`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | light dues | primary | runs |
| FORTRAN | `trebago` | numeric | visibility range model | primary | runs |
| Ada | `eve` | limits | signal timing limits | primary | runs |
| APL | `kron` | arrays | beacon grid | primary | runs |
| MUMPS | `myra` | records | harbour tree | primary | runs |
| JOVIAL | `-` | control | machine control | primary | planned: needs Lingua front-end |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (JOVIAL): see docs/LANGUAGE_CATALOGUE.md for this tier
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `timing_ok`: (critical) signal timing limits (each light's character within its declared period and phase) in Ada as Eve + APL as KRON witnessed
- - `visibility`: (critical) visibility range (nominal range by intensity and the met visibility, integer table) in FORTRAN as Trebago + APL as KRON witnessed
- - `broadcast`: hazard broadcast routing (area to channel) in Ada as Eve
- - `beacons`: beacon grid in APL as KRON
- - `dues`: light dues (minor units, refusing on overflow) in COBOL as Sutra
- - `harbour`: harbour tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two reports of a wreck give different positions: the hazard is BOTH
- the test asserts state 3 and that it is broadcast as BOTH with both positions and its confidence. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id foghorn
matrix 400
name Foghorn
needs peers
needs net
needs journal
needs location
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

