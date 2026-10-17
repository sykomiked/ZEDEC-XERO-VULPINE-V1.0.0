# Criteria — windlass

| | |
|---|---|
| app | `windlass` |
| matrix layer | 340 |
| icon group | L3 |
| wave spec | `wave28/340-windlass.md` |

## Purpose

plans, logs and audits a mine or quarry: ore grades, shifts, safety checks and royalty payments (`files.read`, `files.write`, `search.query`, `ledger.entries`, `ledger.post`, `ledger.hold`). zxvd has no method for gas detector readings: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `ledger.read`
- `ledger.post`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | royalty ledger |
| FORTRAN | trebago | ore grade model |
| Ada | eve | gas and depth limits |
| APL | kron | assay grid |
| MUMPS | myra | crew health tree |
| PHP | php | reports |
| Occam | - | concurrency |
| Simula 67 | - | simulation |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id windlass
matrix 340        # the graph's slot IS the matrix layer
name Windlass
needs fs.read
needs fs.write
needs search
needs ledger.read
needs ledger.post
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

