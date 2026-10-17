# Criteria — firstlight

| | |
|---|---|
| app | `firstlight` |
| matrix layer | 304 |
| icon group | L3 |
| wave spec | `wave25/304-firstlight.md` |

## Purpose

a child's first computer: safe play, reading, puzzles and first coding lessons with parents in charge (`screen.check`, `screen.status`, `files.read`, `files.write`, `play.catalogue`, `play.run`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `screen.time`
- `sys.read`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | pocket-money ledger |
| FORTRAN | trebago | learning pace model |
| Ada | eve | screen time limits |
| APL | kron | puzzle grids |
| MUMPS | myra | child profile tree |
| BASIC | basic | step-by-step teaching |
| Mojo | - | AI |
| Prolog | - | rules |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id firstlight
matrix 304        # the graph's slot IS the matrix layer
name Firstlight
needs fs.read
needs fs.write
needs screen.time
needs sys.read
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

