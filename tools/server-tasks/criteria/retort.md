# Criteria — retort

| | |
|---|---|
| app | `retort` |
| matrix layer | 70 |
| icon group | L1 |
| wave spec | `wave4/070-retort.md` |

## Purpose

the model slot (`model.info`), answers (`swarm.ask`), runs on scratch state (`play.run`), sandboxed functions (`aipi.execute`), energy per run (`power.status`).

## Capabilities (needs — the exact registry names)

- `ai.ask`
- `swarm`
- `sys.read`

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
| Simula 67 | - | simulation |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id retort
matrix 70        # the graph's slot IS the matrix layer
name Retort
needs ai.ask
needs swarm
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

