# Criteria — loupe

| | |
|---|---|
| app | `loupe` |
| matrix layer | 181 |
| icon group | L2 |
| wave spec | `wave15/181-loupe.md` |

## Purpose

investigates incidents by tracing logs (`log.tail`), events (`journal.query`, `journal.verify`) and changes to files (`search.query`, `files.stat`) into a chain of evidence.

## Capabilities (needs — the exact registry names)

- `journal`
- `search`
- `fs.read`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | evidence register |
| FORTRAN | trebago | timeline analysis |
| Ada | eve | chain-of-custody rules |
| APL | kron | event matrix |
| MUMPS | myra | case tree |
| Elixir | - | fault handling |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id loupe
matrix 181        # the graph's slot IS the matrix layer
name Loupe
needs journal
needs search
needs fs.read
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

