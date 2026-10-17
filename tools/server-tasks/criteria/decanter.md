# Criteria — decanter

| | |
|---|---|
| app | `decanter` |
| matrix layer | 232 |
| icon group | L2 |
| wave spec | `wave19/232-decanter.md` |

## Purpose

Fronts the Vino engines (kernel/src/vino, kernel/src/vino_stores, kernel/src/airc for relay time, kernel/src/ipfs_node for storage) as a router: Vino held and quoted (`swap.status`, `swap.quote`, `ledger.balance`), held while a route runs (`ledger.hold`, `ledger.holds`, `ledger.release`), compute from another node (`peer.ask`, `peer.status`), storage (`aipi.put`, `aipi.status`), energy (`power.budget`), switches (`features.get`). Buying AIRC relay time, a storage route and a route list have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `peers`
- `net`
- `swarm`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | Vino ledger |
| FORTRAN | trebago | exchange arithmetic |
| Ada | eve | per-route limits |
| APL | kron | route table |
| MUMPS | myra | holder records |
| Erlang | - | messaging |
| Ruby | ruby | scripting |
| Prolog | - | search |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id decanter
matrix 232        # the graph's slot IS the matrix layer
name Decanter
needs ledger.read
needs ledger.post
needs peers
needs net
needs swarm
needs notify
provides <entry> num|truth <arg>[,<arg>...]
logic <lang> <entry> <<
<source>
.
check <entry> <arg>=<int>[,<int>...] = <answer>[,<answer>...]
```

Rules the loader enforces (a violation refuses the manifest):

- Every `provides` entry must have at least one `logic` line.
- Every `logic` entry must have a `check` line with exact answers.
- The checks' inputs are integers; answers are integers, `n/d`,
  `true`, `false`, `neither`, or the word `both` (two languages
  disagreeing).
- A truth entry's checks use the four-valued answers, never 0/1.
- Inline sources end with a line containing only `.`.

