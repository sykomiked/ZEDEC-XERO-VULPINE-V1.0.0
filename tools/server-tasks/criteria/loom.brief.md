# loom — the complete brief

| | |
|---|---|
| matrix layer | 265 |
| icon group | L2 |
| wave spec | `wave22/265-loom.md` |

## Purpose

Fronts app composition (kernel/src/apps/zxapp.c: manifests, `use`, `provides`, at most 8 `use` lines (ZXAPP_USES); kernel/src/modbind; kernel/src/immigration; docs/CAGE.md). Boards saved in the Hoard (`files.read`, `files.write`), test runs on scratch state (`play.run`), a composed app stored and published by CID (`aipi.put`, `aipi.publish`, `web4.cid`), its functions tried in the sandbox (`aipi.execute`). A Cage run and a manifest-check method have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `fs.read`
- `fs.write`
- `swarm`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | composition log | primary | runs |
| FORTRAN | `trebago` | numeric | budget totals per wire | primary | runs |
| Ada | `eve` | limits | contract checks on each wire | primary | runs |
| APL | `kron` | arrays | board grid | primary | runs |
| MUMPS | `myra` | records | block tree | primary | runs |
| Bourne shell | `-` | jobs | jobs | primary | planned: needs Lingua front-end |
| Racket | `-` | parse | parsing | primary | planned: needs Lingua front-end |
| B | `-` | machine | low-level system | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Bourne shell): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) "this wire is valid" (output kind matches input kind, num or truth, and the argument count) in Ada as Eve + APL as KRON witnessed
- - (critical) budget total over the wires within the app's budget (refusing on overflow) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - board grid (blocks x places) in APL as KRON
- - composition log (wires added and removed, counts refusing on overflow) in COBOL as Sutra
- - block tree (block, version, CID, wires) in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): two blocks give different answers on the same wire, giving BOTH
- the test asserts state 3 and that the board shows both, no silent pick. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id loom
matrix 265
name Loom
needs fs.read
needs fs.write
needs swarm
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

