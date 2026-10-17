# Criteria — hatchery

| | |
|---|---|
| app | `hatchery` |
| matrix layer | 65 |
| icon group | L1 |
| wave spec | `wave7/065-hatchery.md` |

## Purpose

templates (`play.catalogue`), drafts from the swarm (`play.draft`, untrusted), runs on scratch state (`play.run`), publish and run (`aipi.publish`, `aipi.execute`), energy budgets (`power.budget`), the swarm (`swarm.state`).

## Capabilities (needs — the exact registry names)

- `swarm`
- `ai.ask`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| Ada | eve | limits |
| Python | python | AI |
| Go | go | concurrency |
| JCL | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id hatchery
matrix 65        # the graph's slot IS the matrix layer
name Hatchery
needs swarm
needs ai.ask
needs fs.read
needs fs.write
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

