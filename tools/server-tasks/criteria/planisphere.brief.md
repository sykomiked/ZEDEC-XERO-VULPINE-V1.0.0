# planisphere — the complete brief

| | |
|---|---|
| matrix layer | 391 |
| icon group | L3 |
| wave spec | `wave32/391-planisphere.md` |

## Purpose

Your own computer from disc to desktop in one place, every step signed: install (Plinth), firmware and boot (Fusee), devices (Muster), first start (Overture) and community updates (Thicket). Uses only their daemon methods (`sys.disk`, `sys.info`, `update.check`, `journal.verify`, `journal.query`, `features.get`, `seats.list`, `spaces.propose`, `spaces.vote`); the steps they report as missing stay missing here: list them in your report.

## Capabilities (the exact registry names for `needs`)

- `sys.read`
- `fs.read`
- `fs.write`
- `journal`
- `backup`
- `net`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | machine history | primary | runs |
| FORTRAN | `trebago` | numeric | capacity planning | primary | runs |
| Ada | `eve` | limits | refuses unsafe steps | primary | runs |
| APL | `kron` | arrays | machine inventory | primary | runs |
| MUMPS | `myra` | records | configuration tree | primary | runs |
| Crystal | `-` | machine | low-level system | primary | planned: needs Lingua front-end |
| ALGOL 60 | `algol60` | proof | proofs of correctness | primary | runs |
| Haskell | `-` | proof | proofs of correctness | witness | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Crystal): see docs/LANGUAGE_CATALOGUE.md for this tier
- **algol60** (ALGOL 60): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `step_ok`: (critical) "do this step" (its part's check TRUE AND every earlier step done AND the owner confirmed
- any BOTH keeps the last good state) in Ada as Eve + APL as KRON witnessed, joined with `lpres_conjoin` in C
- - `capacity`: (critical) capacity planning (disc, memory and model space needed against what the machine has, bytes) in FORTRAN as Trebago + APL as KRON witnessed
- - `inventory`: machine inventory (parts x status) in APL as KRON
- - `history`: machine history (signed steps, refusing on overflow) in COBOL as Sutra
- - `config`: configuration tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): Fusee passes a boot image and Thicket's auditors disagree on the update inside it: the step is BOTH
- the test asserts state 3 and that the machine stays on its last good state. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |

## The manifest contract

```
zxapp 1
id planisphere
matrix 391
name Planisphere
needs sys.read
needs fs.read
needs fs.write
needs journal
needs backup
needs net
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

