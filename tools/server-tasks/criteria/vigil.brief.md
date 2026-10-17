# vigil — the complete brief

| | |
|---|---|
| matrix layer | 274 |
| icon group | L3 |
| wave spec | `wave23/274-vigil.md` |

## Purpose

watches servers, networks and agents through the night (`sys.info`, `sys.net`, `log.tail`, `journal.query`), waking the right person only when something truly needs a human (`notes.list`). zxvd has no method for readings from other machines (as Headland reports): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `journal`
- `net`
- `notify`
- `peers`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | incident billing | primary | runs |
| FORTRAN | `trebago` | numeric | load forecasts | primary | runs |
| Ada | `eve` | limits | alert thresholds | primary | runs |
| APL | `kron` | arrays | metric arrays | primary | runs |
| MUMPS | `myra` | records | system hierarchy | primary | runs |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| Python | `python` | ai | AI | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **python** (Python): the host tier (via the SDK): tooling and glue, exact integers.

## The spec's Logic clauses (what each component computes)

- `page_now` (Headland's alarm TRUE AND not a soft watch AND the on-call person's shift now) in Ada as Eve + APL as KRON witnessed
- `load_forecast` (next hour's load from the integer trend) in FORTRAN as Trebago + APL as KRON witnessed
- `incident_bill` (minutes paged per incident x the declared rate, minor units) in COBOL as Sutra
- `metrics` arrays in APL as KRON
- `systems` hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id vigil
matrix 274
name Vigil
needs sys.read
needs journal
needs net
needs notify
needs peers
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

