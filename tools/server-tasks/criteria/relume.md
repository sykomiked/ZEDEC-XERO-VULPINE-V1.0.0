# Criteria — relume

| | |
|---|---|
| app | `relume` |
| matrix layer | 273 |
| icon group | L3 |
| wave spec | `wave23/273-relume.md` |

## Purpose

brings a business or household back after a disaster: restores files, books and services in the right order from signed backups (`backup.list`, `backup.verify`, `backup.restore`), checking the books (`ledger.check`) and the journal (`journal.verify`).

## Capabilities (needs — the exact registry names)

- `backup`
- `fs.read`
- `fs.write`
- `journal`
- `ledger.read`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | restored ledgers |
| FORTRAN | trebago | recovery-time model |
| Ada | eve | restore order limits |
| APL | kron | service dependency grid |
| MUMPS | myra | record hierarchy |
| Elixir | - | fault handling |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id relume
matrix 273        # the graph's slot IS the matrix layer
name Relume
needs backup
needs fs.read
needs fs.write
needs journal
needs ledger.read
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

