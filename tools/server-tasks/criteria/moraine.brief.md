# moraine — the complete brief

| | |
|---|---|
| matrix layer | 210 |
| icon group | L2 |
| wave spec | `wave17/210-moraine.md` |

## Purpose

holds keys in escrow with guardians (`recovery.setup`, `recovery.status`, `recovery.approve`, `recovery.veto`, `recovery.claim`, `recovery.finalize`, `recovery.cancel`), grants time-limited access, and logs every release (`journal.query`).

## Capabilities (the exact registry names for `needs`)

- `peers`
- `journal`
- `notify`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | custody register | primary | runs |
| FORTRAN | `trebago` | numeric | key-strength scoring | primary | runs |
| Ada | `eve` | limits | release limits | primary | runs |
| APL | `kron` | arrays | access grid | primary | runs |
| MUMPS | `myra` | records | guardian hierarchy | primary | runs |
| Eiffel | `-` | contracts | contracts | primary | planned: needs Lingua front-end |
| Solidity | `-` | contracts | contracts | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Eiffel): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- "release now" (Heirloom's release due AND access window not over AND requester allowed) in Ada as Eve + APL as KRON witnessed
- custody register (release number, guardian, time, never reused) in COBOL as Sutra
- key-strength score (from lengths and counts only) in FORTRAN as Trebago + APL as KRON witnessed
- access grid in APL as KRON
- guardian hierarchy in MUMPS as MYRA

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |

## The manifest contract

```
zxapp 1
id moraine
matrix 210
name Moraine
needs peers
needs journal
needs notify
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

