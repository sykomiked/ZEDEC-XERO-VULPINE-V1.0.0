# Criteria — druse

| | |
|---|---|
| app | `druse` |
| matrix layer | 324 |
| icon group | L3 |
| wave spec | `wave27/324-druse.md` |

## Purpose

deep cold storage for things that must outlive you: offline seed copies of data, keys and documents (`backup.run`, `backup.verify`, `files.cross`, `sys.disk`, `recovery.status`).

## Capabilities (needs — the exact registry names)

- `backup`
- `fs.read`
- `fs.write`
- `sys.read`
- `peers`
- `journal`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | storage fees |
| FORTRAN | trebago | media decay model |
| Ada | eve | access limits |
| APL | kron | shelf grid |
| MUMPS | myra | holdings tree |
| TRAC | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id druse
matrix 324        # the graph's slot IS the matrix layer
name Druse
needs backup
needs fs.read
needs fs.write
needs sys.read
needs peers
needs journal
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

