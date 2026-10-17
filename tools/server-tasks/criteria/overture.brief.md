# overture — the complete brief

| | |
|---|---|
| matrix layer | 252 |
| icon group | L2 |
| wave spec | `wave20/252-overture.md` |

## Purpose

Fronts first start (kernel/src/seats, kernel/src/ident, kernel/src/terms (the agreement), kernel/src/license, kernel/src/i18n, kernel/src/timesvc for zones). Seats and passkeys (`seats.add`, `seats.list`, `seats.set_lock`, `seats.auto`, `seats.set_auto`, `passkeys.register_begin`, `passkeys.register_finish`), language and simple mode (`xlate.languages`, `ui.get`, `ui.set`), network (`net.command`), moving in from an old machine (`devmesh.join`, `devmesh.confirm_sas`, `files.cross`, `spaces.list`), the clock (`sys.info`). Recording a licence acceptance, setting the time zone and setting the keyboard layout have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `fs.read`
- `fs.write`
- `peers`
- `net`
- `backup`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | setup log | primary | runs |
| FORTRAN | `trebago` | numeric | time zone and clock maths | primary | runs |
| Ada | `eve` | limits | licence must be accepted per user | primary | runs |
| APL | `kron` | arrays | language and layout tables | primary | runs |
| MUMPS | `myra` | records | account records | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Raku | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| Kotlin | `kotlin` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier
- **kotlin** (Kotlin): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "this seat may be used" = the licence accepted by this user (never inherited from another seat) in Ada as Eve + APL as KRON witnessed
- - (critical) local time from UTC minutes and the zone's offset and daylight rule in FORTRAN as Trebago + APL as KRON witnessed
- - language and layout tables (language x layout x script) in APL as KRON
- - setup log as fixed records (step, result), the step counter refusing on overflow, in COBOL as Sutra
- - account records in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the old machine's text size 18 and the new machine's 14 give BOTH
- the test asserts state 3 and that both are shown to the user, neither overwritten. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id overture
matrix 252
name Overture
needs sys.read
needs fs.read
needs fs.write
needs peers
needs net
needs backup
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

