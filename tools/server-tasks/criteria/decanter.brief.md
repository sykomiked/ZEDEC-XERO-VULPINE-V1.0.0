# decanter — the complete brief

| | |
|---|---|
| matrix layer | 232 |
| icon group | L2 |
| wave spec | `wave19/232-decanter.md` |

## Purpose

Fronts the Vino engines (kernel/src/vino, kernel/src/vino_stores, kernel/src/airc for relay time, kernel/src/ipfs_node for storage) as a router: Vino held and quoted (`swap.status`, `swap.quote`, `ledger.balance`), held while a route runs (`ledger.hold`, `ledger.holds`, `ledger.release`), compute from another node (`peer.ask`, `peer.status`), storage (`aipi.put`, `aipi.status`), energy (`power.budget`), switches (`features.get`). Buying AIRC relay time, a storage route and a route list have no method: use only existing methods; list what's missing in your report.

## Capabilities (the exact registry names for `needs`)

- `ledger.read`
- `ledger.post`
- `peers`
- `net`
- `swarm`
- `notify`

## The language map: which language owns which component

| language | runtime | owns | feature | role | status |
|---|---|---|---|---|---|
| C | `c` | view | view and model | primary | runs |
| COBOL | `sutra` | money | Vino ledger | primary | runs |
| FORTRAN | `trebago` | numeric | exchange arithmetic | primary | runs |
| Ada | `eve` | limits | per-route limits | primary | runs |
| APL | `kron` | arrays | route table | primary | runs |
| MUMPS | `myra` | records | holder records | primary | runs |
| Erlang | `-` | messaging | messaging | primary | planned: needs Lingua front-end |
| Ruby | `ruby` | scripting | scripting | primary | planned: SDK host only, no in-app runtime yet |
| Prolog | `-` | search | search | primary | planned: needs Lingua front-end |

## The runtime rules (how each named runtime's source must look)

- **c** (C): the host tier: C11, zero-malloc, fixed buffers, exact integers. The view and model layer; reaches the system through the C ABI.
- **sutra** (COBOL): the pillar (COBOL): records and procedures. A record with PIC pictures, then 'procedure division.' with COMPUTE and IF. A value that does not fit its picture is refused, never cut.
- **trebago** (FORTRAN): the pillar (FORTRAN): exact numerics. 'trebago <entry>' then 'in  <args>' / 'out <result>' / assignments / 'end'. Integer only; sums via sum(i, expr).
- **eve** (Ada): the pillar (Ada): ranges and limits. A package line 'eve <app>', domain declarations '<name> = <lo> .. <hi>', and blocks '<block> { <arg>: <domain> <lo> .. <hi> }'. It declares what is allowed; it cannot compute.
- **kron** (APL): the pillar (APL/K): exact array arithmetic as ONE expression line '<first-row-answer>|<expr with _arg names>'. Multiply, modulus, comparisons, min/max only. No floats.
- **myra** (MUMPS): the pillar (MUMPS): records and indexes. One global line '^<GLOBAL>(<arg>:<pic>, ...) = <field>:<pic>! ...'. The pics are the field widths; the index is the first key.
- **-** (Erlang): see docs/LANGUAGE_CATALOGUE.md for this tier
- **ruby** (Ruby): see docs/LANGUAGE_CATALOGUE.md for this tier

## The spec's Logic clauses (what each component computes)

- - (critical) exchange (Vino minor units to compute ms, relay seconds or storage bytes at the route's rate num/den, remainder carried, never lost) in FORTRAN as Trebago + COBOL as Sutra witnessed
- - (critical) "route allowed" (route on, its plug-in switched on by the owner (off by default), within the per-route cap and the balance) in Ada as Eve + COBOL as Sutra witnessed
- - route table (routes x rate, cap, provider) in APL as KRON
- - Vino ledger lines refusing on overflow in COBOL as Sutra
- - holder records in MUMPS as MYRA
- Paraconsistent (BOTH test, in test_zxapp.c): the provider reports 3600 relay seconds delivered and the holder's meter 1800, giving BOTH
- the test (scratch books) asserts state 3 and that the Vino stays on `ledger.hold`, not released. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C
- never re-derive the tables

## The entries and their exact checks

| entry (pattern) | kind | args | language | exact check |
|---|---|---|---|---|
| `ratio_x1000` | num | `num den` | kron | `check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750` |
| `limit_excess` | num | `value limit` | trebago | `check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000` |
| `composite_key` | num | `high low` | myra | `check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999` |
| `running_state` | truth | `state event` | kron | `check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false` |

## The manifest contract

```
zxapp 1
id decanter
matrix 232
name Decanter
needs ledger.read
needs ledger.post
needs peers
needs net
needs swarm
needs notify
provides <entry> num|truth <arg> <arg> ...
logic <runtime> <entry> <<
<source>
.
check <entry> <arg>=<int>,... = <answer>,...
```

