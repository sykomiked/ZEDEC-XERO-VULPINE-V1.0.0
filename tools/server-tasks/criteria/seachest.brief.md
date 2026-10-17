# seachest — the complete brief

| | |
|---|---|
| matrix layer | 403 |
| icon group | L3 |
| wave spec | `wave33/403-seachest.md` |

## Purpose

Keeps wills, deeds, keys and valuables in sealed custody with timed releases and guardian approvals: the estate (`estate.status`, `estate.manifest`), guardians (`recovery.status`, `recovery.approve`, `recovery.veto`, `recovery.claim`), keys (`passkeys.list`), records in the Hoard (`files.read`, `files.write`), the record (`journal.query`, `journal.verify`).

## Capabilities (the exact registry names for `needs`)

- `peers`
- `journal`
- `fs.read`
- `fs.write`
- `sys.read`
- `ledger.read`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | custody ledger | primary | runs |
| FORTRAN | `trebago` | numeric | release scheduling | primary | runs |
| Ada | `eve` | limits | access limits | primary | runs |
| APL | `kron` | arrays | item grid | primary | runs |
| MUMPS | `myra` | records | beneficiary tree | primary | runs |
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

- - `access_ok`: (critical) access limits (a release only when Heirloom's release due AND the guardians approve AND no claim is open) in Ada as Eve + COBOL as Sutra witnessed
- - `schedule`: (critical) release scheduling (dates from the will's terms, days since the trigger) in FORTRAN as Trebago + APL as KRON witnessed
- - `custody`: custody ledger (items in and out, refusing on overflow) in COBOL as Sutra
- - `items`: item grid in APL as KRON
- - `beneficiaries`: beneficiary tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two claims to one deed: it is BOTH
- the test asserts state 3 and that nothing is released until resolved. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id seachest
matrix 403
name Seachest
needs peers
needs journal
needs fs.read
needs fs.write
needs sys.read
needs ledger.read
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

