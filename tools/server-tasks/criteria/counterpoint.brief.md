# counterpoint — the complete brief

| | |
|---|---|
| matrix layer | 378 |
| icon group | L3 |
| wave spec | `wave31/378-counterpoint.md` |

## Purpose

Shares money, plans and chores as a couple, with joint goals and each person's view kept: joint accounts (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.hold`), plans in the Hoard (`files.read`, `files.write`), the shared clock (`sys.info`).

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `sys.read`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | joint account | primary | runs |
| FORTRAN | `trebago` | numeric | budget forecasting | primary | runs |
| Ada | `eve` | limits | spending limits | primary | runs |
| APL | `kron` | arrays | chore grid | primary | runs |
| MUMPS | `myra` | records | household tree | primary | runs |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| Simula I | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| Delphi | `-` | structure | structure | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (Simula I): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `spend_ok`: (critical) spending limits (each person's solo cap
- above it both approve) in Ada as Eve + COBOL as Sutra witnessed
- - `forecast`: (critical) budget forecast (monthly sums and the end-of-month balance from planned items) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - `joint`: joint account lines (minor units, refusing on overflow) in COBOL as Sutra
- - `chores`: chore grid (people x tasks x weeks, fair share counts) in APL as KRON
- - `planner`: shared plan structure (goals, owners, steps) in Ada as Eve
- - `household`: household tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the partners record the same dinner as 64.00 and 46.00: the expense is BOTH
- the test (scratch books) asserts state 3 and that both entries stay until they talk. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id counterpoint
matrix 378
name Counterpoint
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs sys.read
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

