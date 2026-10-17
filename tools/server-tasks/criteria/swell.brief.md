# swell — the complete brief

| | |
|---|---|
| matrix layer | 251 |
| icon group | L2 |
| wave spec | `wave20/251-swell.md` |

## Purpose

Rehearses ensembles: a rehearsal hall (`pspace.create`, `pspace.members`, `hall.load`, `hall.play`, `hall.pause`, `hall.seek`, `hall.status`), players' devices (`devmesh.devices`, `net.peers`), scores and parts in the Hoard (`files.read`, `files.write`), the clock (`sys.info`). Audio in and out has no method (as Fiddlehead): use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `peers`
- `net`
- `fs.read`
- `fs.write`
- `sys.read`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | player roster | primary | runs |
| FORTRAN | `trebago` | numeric | tempo sync | primary | runs |
| Ada | `eve` | limits | latency limits | primary | runs |
| APL | `kron` | arrays | score matrix | primary | runs |
| MUMPS | `myra` | records | part tree | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Simula 67 | `-` | simulation | simulation | primary | planned: needs Lingua front-end |
| System/360 assembler | `-` | machine | low-level system | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) click time per device (start + n x 60000000 / bpm microseconds, minus that device's measured latency) in FORTRAN as Trebago + APL as KRON witnessed
- - (critical) latency limits (jitter at most 30 ms, else the device is marked out of sync) in Ada as Eve + APL as KRON witnessed
- - score matrix (bars x parts) in APL as KRON
- - player roster and part tree in MUMPS as MYRA
- - Sutra is left out: no money or counted business records
- the roster is MYRA's
- Paraconsistent (BOTH test, in test_zxapp.c): the conductor's and the section leader's edits to bar 12 differ, giving BOTH
- the test asserts state 3 and that both edits are kept until the conductor decides. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `difference` | num | `a b` | trebago | `check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `count_ok` | truth | `count max` | kron | `check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id swell
matrix 251
name Swell
needs peers
needs net
needs fs.read
needs fs.write
needs sys.read
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

