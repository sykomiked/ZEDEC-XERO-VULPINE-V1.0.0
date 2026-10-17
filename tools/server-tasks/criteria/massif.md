# Criteria — massif

| | |
|---|---|
| app | `massif` |
| matrix layer | 256 |
| icon group | L2 |
| wave spec | `wave21/256-massif.md` |

## Purpose

Spreads large integer compute jobs across machines and tracks results: functions and data (`aipi.put`, `aipi.get`, `aipi.publish`, `aipi.execute`, `aipi.status`), other nodes (`peer.ask`, `peer.status`, `net.peers`), energy and budgets (`power.budget`, `power.status`, `swarm.state`).

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `swarm`
- `sys.read`
- `ledger.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | job accounting |
| FORTRAN | trebago | numerical kernels |
| Ada | eve | resource limits |
| APL | kron | work-unit array |
| MUMPS | myra | job tree |
| Occam | - | concurrency |
| Bash | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id massif
matrix 256        # the graph's slot IS the matrix layer
name Massif
needs peers
needs net
needs swarm
needs sys.read
needs ledger.read
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

