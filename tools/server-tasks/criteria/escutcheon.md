# Criteria — escutcheon

| | |
|---|---|
| app | `escutcheon` |
| matrix layer | 315 |
| icon group | L3 |
| wave spec | `wave26/315-escutcheon.md` |

## Purpose

insurance for households and co-ops: policies, premiums, claims, evidence and mutual-aid grants (`ledger.accounts`, `ledger.entries`, `ledger.post`, `ledger.hold`, `files.read`, `files.write`, `journal.query`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | premiums and mutual-aid grants |
| FORTRAN | trebago | risk model |
| Ada | eve | cover limits |
| APL | kron | claim grid |
| MUMPS | myra | policy holder tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id escutcheon
matrix 315        # the graph's slot IS the matrix layer
name Escutcheon
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs journal
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

