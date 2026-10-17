# Criteria — folio

| | |
|---|---|
| app | `folio` |
| matrix layer | 39 |
| icon group | L1 |
| wave spec | `wave6/039-folio.md` |

## Purpose

letters, reports and books as files (`files.read`, `files.write`), printed with `print.status`, `print.submit`, `print.job`.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `print`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| TRAC | - | text |
| TeX | - | typesetting |
| COBOL | sutra | money and ledgers |
| C++ | cpp | money and ledgers |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id folio
matrix 39        # the graph's slot IS the matrix layer
name Folio
needs fs.read
needs fs.write
needs print
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

