# sexton — the complete brief

| | |
|---|---|
| matrix layer | 195 |
| icon group | L2 |
| wave spec | `wave16/195-sexton.md` |

## Purpose

responds to outages and incidents: raises alarms (`log.tail`, `journal.query`, `sys.info`), rallies people (`chat.post`, `call.start` on a person's say), and runs recovery (`backup.verify`).

## Capabilities (the exact registry names for `needs`)

- `journal`
- `sys.read`
- `peers`
- `net`
- `notify`
- `backup`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | incident record | primary | runs |
| FORTRAN | `trebago` | numeric | impact estimate | primary | runs |
| Ada | `eve` | limits | response deadlines | primary | runs |
| APL | `kron` | arrays | alert matrix | primary | runs |
| MUMPS | `myra` | records | on-call tree | primary | runs |
| Elixir | `-` | faults | fault handling | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Elixir): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "escalate" (no acknowledgement within the response deadline) in Ada as Eve + APL as KRON witnessed
- impact (seats affected x minutes) exact in FORTRAN as Trebago
- incident record (incident number, opened, acknowledged, closed, never reused) in COBOL as Sutra
- alert matrix (monitor by state) in APL as KRON
- on-call tree in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id sexton
matrix 195
name Sexton
needs journal
needs sys.read
needs peers
needs net
needs notify
needs backup
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

