# bulwark — the complete brief

| | |
|---|---|
| matrix layer | 375 |
| icon group | L3 |
| wave spec | `wave31/375-bulwark.md` |

## Purpose

Protects an organisation's keys, assets and accounts with layered guardians, approvals and recovery: keys and devices (`passkeys.list`, `passkeys.revoke`, `devmesh.devices`, `devmesh.revoke`), guardians and recovery (`recovery.setup`, `recovery.status`, `recovery.approve`, `recovery.veto`, `recovery.claim`), transfers held (`ledger.hold`, `ledger.holds`, `ledger.release`), the record (`journal.query`, `journal.verify`).

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `peers`
- `ledger.read`
- `ledger.post`
- `journal`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | asset register | primary | runs |
| FORTRAN | `trebago` | numeric | risk scoring | primary | runs |
| Ada | `eve` | limits | withdrawal limits | primary | runs |
| APL | `kron` | arrays | access matrix | primary | runs |
| MUMPS | `myra` | records | guardian tree | primary | runs |
| Smalltalk | `-` | objects | domain objects | primary | planned: needs Lingua front-end |
| Erlang | `-` | faults | fault handling | primary | planned: needs Lingua front-end |
| Oberon | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Smalltalk): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `transfer_ok`: (critical) withdrawal limits (k of n guardians approved AND within the period's cap AND no veto) in Ada as Eve + COBOL as Sutra witnessed
- - `risk`: (critical) risk score (exposure, guardian spread, expiring keys, weighted) in FORTRAN as Trebago + APL as KRON witnessed
- - `access`: access matrix (people x assets) in APL as KRON
- - `register`: asset register (minor units, refusing on overflow) in COBOL as Sutra
- - `roles`: role hierarchy and approval layers in Ada as Eve
- - `guardians`: guardian tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two guardians approve and one vetoes the same transfer: it is BOTH
- the test (scratch books) asserts state 3 and the transfer frozen on `ledger.hold`. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `modular_draw` | num | `seed members step` | trebago | `check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id bulwark
matrix 375
name Bulwark
needs sys.read
needs peers
needs ledger.read
needs ledger.post
needs journal
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

