# Criteria — skep

| | |
|---|---|
| app | `skep` |
| matrix layer | 185 |
| icon group | L2 |
| wave spec | `wave15/185-skep.md` |

## Purpose

coordinates a swarm of agents and nodes on shared work (`swarm.state`, `swarm.earnings`, `net.peers`, `peer.status`) with bounties and patronage grants from the books (`ledger.balance`).

## Capabilities (needs — the exact registry names)

- `swarm`
- `peers`
- `net`
- `ledger.read`
- `ledger.post`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | grant ledger |
| FORTRAN | trebago | load balancing |
| Ada | eve | agent budgets |
| APL | kron | task matrix |
| MUMPS | myra | colony tree |
| Mojo | - | AI |
| Occam | - | concurrency |
| Gleam | - | messaging |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id skep
matrix 185        # the graph's slot IS the matrix layer
name Skep
needs swarm
needs peers
needs net
needs ledger.read
needs ledger.post
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

