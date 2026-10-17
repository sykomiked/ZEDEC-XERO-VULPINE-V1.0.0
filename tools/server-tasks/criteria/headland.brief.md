# headland — the complete brief

| | |
|---|---|
| matrix layer | 215 |
| icon group | L2 |
| wave spec | `wave17/215-headland.md` |

## Purpose

watches a machine or fleet for trouble (`sys.info`, `sys.disk`, `sys.net`, `log.tail`, `journal.query`) and guides the owner with prioritised alerts (`notes.list`). zxvd has no method for readings from other machines in the fleet: use only existing methods; list what's missing in your report.

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
| COBOL | `sutra` | money | alert records | primary | runs |
| FORTRAN | `trebago` | numeric | trend detection | primary | runs |
| Ada | `eve` | limits | alarm thresholds | primary | runs |
| APL | `kron` | arrays | metric grid | primary | runs |
| MUMPS | `myra` | records | fleet hierarchy | primary | runs |
| Go | `go` | concurrency | concurrency | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- trend (integer least-squares slope over the last n readings) in FORTRAN as Trebago + APL as KRON witnessed
- "raise an alarm" (over the threshold, or a pair of readings BOTH) in Ada as Eve + APL as KRON witnessed
- alert priority order in APL as KRON
- metric grid in APL as KRON
- fleet hierarchy in MUMPS as MYRA

## The entries and their exact checks

(no library pattern matched; author the entries from the clauses)

## The manifest contract

```
zxapp 1
id headland
matrix 215
name Headland
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

