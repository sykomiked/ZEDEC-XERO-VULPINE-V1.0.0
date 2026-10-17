# Criteria — planisphere

| | |
|---|---|
| app | `planisphere` |
| matrix layer | 391 |
| icon group | L3 |
| wave spec | `wave32/391-planisphere.md` |

## Purpose

Your own computer from disc to desktop in one place, every step signed: install (Plinth), firmware and boot (Fusee), devices (Muster), first start (Overture) and community updates (Thicket). Uses only their daemon methods (`sys.disk`, `sys.info`, `update.check`, `journal.verify`, `journal.query`, `features.get`, `seats.list`, `spaces.propose`, `spaces.vote`); the steps they report as missing stay missing here: list them in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `fs.write`
- `journal`
- `backup`
- `net`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | machine history |
| FORTRAN | trebago | capacity planning |
| Ada | eve | refuses unsafe steps |
| APL | kron | machine inventory |
| MUMPS | myra | configuration tree |
| Crystal | - | low-level system |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id planisphere
matrix 391        # the graph's slot IS the matrix layer
name Planisphere
needs sys.read
needs fs.read
needs fs.write
needs journal
needs backup
needs net
needs peers
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

