# spinnaker — the complete brief

| | |
|---|---|
| matrix layer | 366 |
| icon group | L3 |
| wave spec | `wave30/366-spinnaker.md` |

## Purpose

Plans and logs flights for small aircraft and drones: routes and places (`place.here`, `place.list`, `place.add`), weather from station records, checklists and maintenance in the Hoard (`files.read`, `files.write`), the link (`sys.net`). Airspace data and aircraft telemetry have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `location`
- `sys.read`
- `fs.read`
- `fs.write`
- `net`
- `camera`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | landing fees | primary | runs |
| FORTRAN | `trebago` | numeric | flight dynamics | primary | runs |
| Ada | `eve` | limits | weight and altitude limits | primary | runs |
| APL | `kron` | arrays | airspace grid | primary | runs |
| MUMPS | `myra` | records | airframe tree | primary | runs |
| IEC 61131-3 Structured Text | `-` | control | machine control | primary | planned: needs Lingua front-end |
| LISP | `lisp` | lists | lists | primary | runs |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (IEC 61131-3 Structured Text): see docs/LANGUAGE_CATALOGUE.md for this tier
- **lisp** (LISP): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `wb_ok`: (critical) weight and balance (total mass and moment within the envelope) in Ada as Eve + APL as KRON witnessed
- - `alt_ok`: (critical) altitude limits (planned altitude within the airspace band and the craft's ceiling) in Ada as Eve + APL as KRON witnessed
- - `dynamics`: flight dynamics (take-off distance and climb from integer performance tables by mass and density altitude) in FORTRAN as Trebago
- - `airspace`: airspace grid in APL as KRON
- - `checklist`: checklist sequencing (items in order, none skipped) in Ada as Eve
- - `fees`: landing fees (minor units, refusing on overflow) in COBOL as Sutra
- - `airframe`: airframe tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two altimeters read 1200 ft and 1450 ft: altitude is BOTH
- the test asserts state 3 and a crosscheck prompt, neither trusted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `grade_band` | num | `value b1 b2 b3 b4` | kron | `check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id spinnaker
matrix 366
name Spinnaker
needs location
needs sys.read
needs fs.read
needs fs.write
needs net
needs camera
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

