# Criteria — riddle

| | |
|---|---|
| app | `riddle` |
| matrix layer | 104 |
| icon group | L1 |
| wave spec | `wave9/104-riddle.md` |

## Purpose

logic and number puzzles; saves in the Banker (`files.read`, `files.write`); hints from `swarm.ask`.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| Mojo | - | AI |
| Prolog | - | rules |
| BASIC | basic | step-by-step teaching |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id riddle
matrix 104        # the graph's slot IS the matrix layer
name Riddle
needs fs.read
needs fs.write
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

