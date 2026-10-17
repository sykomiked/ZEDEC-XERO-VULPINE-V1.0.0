# Criteria — rigging

| | |
|---|---|
| app | `rigging` |
| matrix layer | 381 |
| icon group | L3 |
| wave spec | `wave32/381-rigging.md` |

## Purpose

Orchestrates many services, agents and machines from one console, with budgets and safe rollbacks: agents (`swarm.state`, `swarm.ask`), engines (`features.get`, `features.set` owner-approved), machines (`net.peers`, `devmesh.devices`, `sys.info`), budgets (`power.budget`, `power.top`), versions to roll back to (`backup.versions`, `backup.restore` as a new item).

## Capabilities (needs — the exact registry names)

- `swarm`
- `ai.ask`
- `peers`
- `net`
- `sys.read`
- `backup`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | compute billing |
| FORTRAN | trebago | load forecasting |
| Ada | eve | budget and rate limits |
| APL | kron | job arrays |
| MUMPS | myra | service tree |
| Occam | - | concurrency |
| Elixir | - | fault handling |
| JCL | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id rigging
matrix 381        # the graph's slot IS the matrix layer
name Rigging
needs swarm
needs ai.ask
needs peers
needs net
needs sys.read
needs backup
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

