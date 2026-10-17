# topsail — the complete brief

| | |
|---|---|
| matrix layer | 399 |
| icon group | L3 |
| wave spec | `wave33/399-topsail.md` |

## Purpose

Hosts apps and files on your own devices and trusted peers as one personal cloud: sync (`spaces.list`, `files.cross`, `backup.run`, `backup.list`), devices and peers (`devmesh.devices`, `net.peers`, `peer.status`), the cloud top (`spaces.get`, `spaces.set_cid`, `aipi.put`, `aipi.get`), budgets (`power.budget`).

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `backup`
- `peers`
- `net`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | hosting ledger | primary | runs |
| FORTRAN | `trebago` | numeric | capacity model | primary | runs |
| Ada | `eve` | limits | resource caps | primary | runs |
| APL | `kron` | arrays | replica grid | primary | runs |
| MUMPS | `myra` | records | tenant tree | primary | runs |
| Gleam | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Elixir | `-` | faults | fault handling | primary | planned: needs Lingua front-end |
| Nim | `-` | machine | low-level system | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Gleam): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `cap_ok`: (critical) resource caps (each tenant within its storage and compute share) in Ada as Eve + APL as KRON witnessed
- - `capacity`: (critical) capacity model (bytes and replicas per device, headroom) in FORTRAN as Trebago + APL as KRON witnessed
- - `failover`: failover choice (next replica by health and recency) in Ada as Eve
- - `replicas`: replica grid in APL as KRON
- - `ledger`: hosting ledger (refusing on overflow) in COBOL as Sutra
- - `tenants`: tenant tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two replicas diverge after a network split: the file is BOTH
- the test asserts state 3 and that both are kept until reconciled. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `share_x100` | num | `part whole` | kron | `check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000` |
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id topsail
matrix 399
name Topsail
needs fs.read
needs fs.write
needs backup
needs peers
needs net
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

