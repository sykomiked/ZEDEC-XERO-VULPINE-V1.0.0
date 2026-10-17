# Criteria — rotunda

| | |
|---|---|
| app | `rotunda` |
| matrix layer | 331 |
| icon group | L3 |
| wave spec | `wave27/331-rotunda.md` |

## Purpose

books shared research instruments and facilities: time slots, training, usage logs and cost sharing (`files.read`, `files.write`, `ledger.entries`, `ledger.post`, `sys.info`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | usage charges |
| FORTRAN | trebago | slot optimisation |
| Ada | eve | operator certification limits |
| APL | kron | booking grid |
| MUMPS | myra | facility tree |
| K | kron | queries |
| Gleam | - | messaging |
| Scheme | scheme | symbolic reasoning |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id rotunda
matrix 331        # the graph's slot IS the matrix layer
name Rotunda
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs sys.read
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

