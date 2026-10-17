# Criteria — scree

| | |
|---|---|
| app | `scree` |
| matrix layer | 219 |
| icon group | L2 |
| wave spec | `wave18/219-scree.md` |

## Purpose

Builds and plays logic and maths puzzles that adapt to a learner's level; puzzles and progress in the Hoard (`files.read`, `files.write`), hints from `swarm.ask` held until the learner asks, time from `screen.check`.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ai.ask`
- `screen.time`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | score records |
| FORTRAN | trebago | difficulty curve |
| Ada | eve | hint limits |
| APL | kron | puzzle grid |
| MUMPS | myra | learner progress tree |
| Python | python | AI |
| BASIC | basic | step-by-step teaching |
| JCL | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id scree
matrix 219        # the graph's slot IS the matrix layer
name Scree
needs fs.read
needs fs.write
needs ai.ask
needs screen.time
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

