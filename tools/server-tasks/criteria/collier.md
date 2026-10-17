# Criteria — collier

| | |
|---|---|
| app | `collier` |
| matrix layer | 163 |
| icon group | L2 |
| wave spec | `wave13/163-collier.md` |

## Purpose

digs through datasets and files in the Hoard (`search.query`, `files.list`, `files.read`, `files.write`) to extract, clean and label the useful records.

## Capabilities (needs — the exact registry names)

- `search`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | provenance ledger |
| FORTRAN | trebago | statistics |
| Ada | eve | privacy limits |
| APL | kron | column arrays |
| MUMPS | myra | source tree |
| SQL | - | queries |
| PostScript | - | page layout |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id collier
matrix 163        # the graph's slot IS the matrix layer
name Collier
needs search
needs fs.read
needs fs.write
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

