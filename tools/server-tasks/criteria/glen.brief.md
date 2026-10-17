# glen — the complete brief

| | |
|---|---|
| matrix layer | 220 |
| icon group | L2 |
| wave spec | `wave18/220-glen.md` |

## Purpose

Runs a family hub: shared calendar and contacts in the Hoard (`files.read`, `files.write`), the family's space and members (`spaces.list`, `spaces.get`), seats (`seats.list`), screen-time rules (`screen.status`, `screen.check`, `screen.set_limit` only after the guardians agree), recovery guardians (`recovery.status`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `screen.time`
- `sys.read`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | household records | primary | runs |
| FORTRAN | `trebago` | numeric | schedule load | primary | runs |
| Ada | `eve` | limits | child limits | primary | runs |
| APL | `kron` | arrays | availability grid | primary | runs |
| MUMPS | `myra` | records | family tree | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Prolog | `-` | rules | rules | primary | planned: needs Lingua front-end |
| Java | `java` | objects | domain objects | primary | planned: SDK host only, no in-app runtime yet |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier
- **java** (Java): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "this child rule takes effect" = within the child limits AND approved by the guardians Heirloom's block requires, in Ada as Eve + APL as KRON witnessed
- - schedule load (minutes booked per person per day) exact in FORTRAN as Trebago
- - child limits (minutes 0..1440, bedtime as minutes of day) in Ada as Eve
- - availability grid (family members x 15-minute slots, the common free times) in APL as KRON
- - family tree (members, guardians, children, contacts) in MUMPS as MYRA
- - Sutra is left out: Glen keeps no money or counted business records
- household records are MYRA's
- Paraconsistent (BOTH test, in test_zxapp.c): parent A sets 60 minutes and parent B 90 minutes for the same child on the same day, giving BOTH
- the test asserts state 3, both shown for agreement, and the earlier rule still applies (the last edit does not win). Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id glen
matrix 220
name Glen
needs fs.read
needs fs.write
needs screen.time
needs sys.read
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

