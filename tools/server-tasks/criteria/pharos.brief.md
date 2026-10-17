# pharos — the complete brief

| | |
|---|---|
| matrix layer | 326 |
| icon group | L3 |
| wave spec | `wave27/326-pharos.md` |

## Purpose

broadcasts emergency warnings across radio, mesh and screens, confirming who has received them (`ehop.bands`, `ehop.seal`, `harmonic.encode`, `pspace.post`, `chat.post`, `net.peers`), each sent after a person approves. zxvd has no method for screen broadcast to other devices: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `net`
- `peers`
- `notify`
- `sys.read`
- `journal`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | warning logs | primary | runs |
| FORTRAN | `trebago` | numeric | hazard spread model | primary | runs |
| Ada | `eve` | limits | broadcast priority limits | primary | runs |
| APL | `kron` | arrays | coverage grid | primary | runs |
| MUMPS | `myra` | records | region tree | primary | runs |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| LabVIEW | `-` | signal | signals | primary | planned: needs Lingua front-end |
| TypeScript | `typescript` | view | view and model | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (LabVIEW): see docs/LANGUAGE_CATALOGUE.md for this tier
- **typescript** (TypeScript): the host tier: the browser surface, typed.

## The spec's Logic clauses (what each component computes)

- `priority_ok` (a warning's priority decides its channel order
- a lower one never pre-empts a higher one) in Ada as Eve + APL as KRON witnessed
- `hazard_spread` (area reached over time from the declared spread rate) in FORTRAN as Trebago + APL as KRON witnessed
- `warning_log` (warning number, receipts counted, never reused) in COBOL as Sutra
- `coverage` grid (region by channel) in APL as KRON
- `regions` tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |

## The manifest contract

```
zxapp 1
id pharos
matrix 326
name Pharos
needs net
needs peers
needs notify
needs sys.read
needs journal
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

