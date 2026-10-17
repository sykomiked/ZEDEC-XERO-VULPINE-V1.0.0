# Criteria — volute

| | |
|---|---|
| app | `volute` |
| matrix layer | 280 |
| icon group | L3 |
| wave spec | `wave23/280-volute.md` |

## Purpose

runs a fish or shellfish farm: water readings, feeding schedules, stock health and harvest sales (`files.read`, `files.write`, `ledger.entries`, `ledger.post`, `ledger.hold`, `place.here`). zxvd has no method for water probe readings: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `location`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | harvest sales |
| FORTRAN | trebago | oxygen and growth model |
| Ada | eve | stocking density limits |
| APL | kron | pen sensor arrays |
| MUMPS | myra | stock health records |
| Gleam | - | messaging |
| Bash | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id volute
matrix 280        # the graph's slot IS the matrix layer
name Volute
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs location
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

