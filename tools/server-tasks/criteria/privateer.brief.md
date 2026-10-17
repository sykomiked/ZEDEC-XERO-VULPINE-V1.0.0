# privateer — the complete brief

| | |
|---|---|
| matrix layer | 386 |
| icon group | L3 |
| wave spec | `wave32/386-privateer.md` |

## Purpose

Hunts threats on the network and devices, tests defences safely and contains intrusions: events (`log.tail`, `journal.query`, `journal.verify`), files (`files.stat`, `search.query`), network mode (`net.command` off/lan, owner-approved), peers (`net.peers`, `devmesh.devices`, `devmesh.revoke` owner-approved), switches (`features.get`).

## Capabilities (the exact registry names for `needs`)

- `journal`
- `search`
- `fs.read`
- `sys.read`
- `net`
- `peers`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | incident costs | primary | runs |
| FORTRAN | `trebago` | numeric | anomaly scoring | primary | runs |
| Ada | `eve` | limits | containment limits | primary | runs |
| APL | `kron` | arrays | event matrix | primary | runs |
| MUMPS | `myra` | records | asset tree | primary | runs |
| Go | `go` | messaging | messaging | primary | planned: SDK host only, no in-app runtime yet |
| BCPL | `-` | machine | low-level system | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **go** (Go): see docs/LANGUAGE_CATALOGUE.md for this tier
- **-** (BCPL): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - `contain_ok`: (critical) containment limits (isolate only the named device or path
- never the owner's last admin device) in Ada as Eve + APL as KRON witnessed
- - `anomaly`: (critical) anomaly score (events per minute against the baseline by kind, integer z-like score) in FORTRAN as Trebago + APL as KRON witnessed
- - `alerts`: alert routing (who hears which severity) in Ada as Eve
- - `events`: event matrix in APL as KRON
- - `costs`: incident costs (minor units, refusing on overflow) in COBOL as Sutra
- - `assets`: asset tree in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): one detector calls an event benign and another malicious: it is BOTH
- the test asserts state 3 and that the item is sandboxed (held), neither ignored nor blocked outright. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `product` | num | `a b` | sutra | `check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id privateer
matrix 386
name Privateer
needs journal
needs search
needs fs.read
needs sys.read
needs net
needs peers
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

