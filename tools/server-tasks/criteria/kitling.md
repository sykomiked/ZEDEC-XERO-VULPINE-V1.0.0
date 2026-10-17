# Criteria — kitling

| | |
|---|---|
| app | `kitling` |
| matrix layer | 62 |
| icon group | L1 |
| wave spec | `wave1/062-kitling.md` |

## Purpose

the companion's DNA, mood and generation (`companion.dna`, `companion.mood`, `companion.generation`), talk (`swarm.ask`), the swarm's state (`swarm.state`).

## Capabilities (needs — the exact registry names)

- `swarm`
- `ai.ask`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| Ada | eve | limits |
| Mojo | - | AI |
| Occam | - | concurrency |
| Clojure | - | symbolic reasoning |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id kitling
matrix 62        # the graph's slot IS the matrix layer
name Kitling
needs swarm
needs ai.ask
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

