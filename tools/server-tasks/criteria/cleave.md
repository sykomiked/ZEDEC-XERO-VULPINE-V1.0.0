# Criteria — cleave

| | |
|---|---|
| app | `cleave` |
| matrix layer | 310 |
| icon group | L3 |
| wave spec | `wave26/310-cleave.md` |

## Purpose

stress tests apps and systems by breaking them on purpose and recording how and where they fail (`play.run` on scratch state, `sys.info`, `files.write`).

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `fs.write`
- `ai.ask`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | test budget |
| FORTRAN | trebago | load generation |
| Ada | eve | blast radius limits |
| APL | kron | failure matrix |
| MUMPS | myra | component tree |
| Elixir | - | fault handling |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cleave
matrix 310        # the graph's slot IS the matrix layer
name Cleave
needs sys.read
needs fs.read
needs fs.write
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

