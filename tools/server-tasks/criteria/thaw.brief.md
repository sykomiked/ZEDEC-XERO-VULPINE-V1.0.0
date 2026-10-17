# thaw — the complete brief

| | |
|---|---|
| matrix layer | 266 |
| icon group | L2 |
| wave spec | `wave22/266-thaw.md` |

## Purpose

Rebuilds a whole machine after disaster from backups, keys and settings (kernel/src/recover): backups (`backup.list`, `backup.verify`, `backup.versions`, `backup.restore`), checks (`journal.verify`, `ledger.check`, `sys.disk`), guardians (`recovery.status`, `recovery.claim`), spaces and files (`spaces.list`, `files.cross`), switches (`features.get`).

## Capabilities (the exact registry names for `needs`)

- `backup`
- `fs.read`
- `fs.write`
- `journal`
- `ledger.read`
- `sys.read`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | restore records | primary | runs |
| FORTRAN | `trebago` | numeric | restore time estimate | primary | runs |
| Ada | `eve` | limits | integrity limits | primary | runs |
| APL | `kron` | arrays | block map | primary | runs |
| MUMPS | `myra` | records | system tree | primary | runs |
| Erlang | `-` | faults | fault handling | primary | planned: needs Lingua front-end |
| JCL | `-` | jobs | jobs | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "may restore this item" (`backup.verify` ok AND chain intact AND it fits) in Ada as Eve + APL as KRON witnessed
- - (critical) restore byte totals (refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- - restore time estimate (bytes / measured rate) in FORTRAN as Trebago
- - block map (items x restored) in APL as KRON
- - system tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two backups of one folder disagree, giving BOTH
- the test (scratch Hoard) asserts state 3 and that both are restored side by side as new items. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `exact_total` | num | `n a b c d` | trebago | `check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0` |
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id thaw
matrix 266
name Thaw
needs backup
needs fs.read
needs fs.write
needs journal
needs ledger.read
needs sys.read
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

