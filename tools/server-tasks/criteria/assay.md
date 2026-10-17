# Criteria — assay

| | |
|---|---|
| app | `assay` |
| matrix layer | 72 |
| icon group | L1 |
| wave spec | `wave2/072-assay.md` |

## Purpose

run test prompts against the swarm's agents and score them (`swarm.state` agents and audit, `swarm.ask` under the budget gate, `play.run` for scored templates on scratch state).

## Capabilities (needs — the exact registry names)

- `swarm`
- `ai.ask`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | statistics |
| Ada | eve | limits |
| Mojo | - | AI |
| Occam | - | concurrency |
| Racket | - | symbolic reasoning |
| S | - | statistics |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id assay
matrix 72        # the graph's slot IS the matrix layer
name Assay
needs swarm
needs ai.ask
needs sys.read
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

