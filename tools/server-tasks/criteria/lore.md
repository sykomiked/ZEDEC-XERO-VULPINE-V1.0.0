# Criteria — lore

| | |
|---|---|
| app | `lore` |
| matrix layer | 67 |
| icon group | L1 |
| wave spec | `wave7/067-lore.md` |

## Purpose

the owner's notes and files (`files.list`, `files.read`, `search.query`, `search.refresh`), read by the companion (`swarm.ask`) only with leave.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `search`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| Ada | eve | limits |
| Python | python | AI |
| Occam | - | concurrency |
| Mathematica | - | symbolic reasoning |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id lore
matrix 67        # the graph's slot IS the matrix layer
name Lore
needs fs.read
needs search
needs ai.ask
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

