# Criteria — orlop

| | |
|---|---|
| app | `orlop` |
| matrix layer | 398 |
| icon group | L3 |
| wave spec | `wave33/398-orlop.md` |

## Purpose

Stores and queries your structured data with versioned tables, signed changes and offline copies: tables in the Hoard (`files.read`, `files.write`, `files.list`), versions (`backup.versions`, `backup.list`), the change record (`journal.query`, `journal.verify`), keys in escrow (`recovery.status`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `backup`
- `journal`
- `peers`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | record schemas |
| FORTRAN | trebago | query statistics |
| Ada | eve | quota limits |
| APL | kron | column arrays |
| MUMPS | myra | hierarchical keys |
| F# | dotnet | typed data |
| SQL | - | queries |
| Julia | - | matrices and images |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id orlop
matrix 398        # the graph's slot IS the matrix layer
name Orlop
needs fs.read
needs fs.write
needs backup
needs journal
needs peers
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

