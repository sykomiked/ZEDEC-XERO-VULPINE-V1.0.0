# Criteria — lode

| | |
|---|---|
| app | `lode` |
| matrix layer | 246 |
| icon group | L2 |
| wave spec | `wave20/246-lode.md` |

## Purpose

Appraises and registers precious items with photos, provenance and value records: photos and records in the Hoard (`files.read`, `files.write`, room from `sys.disk`), the trail (`journal.query`), values in the books (`ledger.accounts`, `ledger.balance`, `ledger.post`).

## Capabilities (needs — the exact registry names)

- `camera`
- `fs.read`
- `fs.write`
- `sys.read`
- `journal`
- `ledger.read`
- `ledger.post`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | valuation records |
| FORTRAN | trebago | carat and cut metrics |
| Ada | eve | grading rules |
| APL | kron | grade matrix |
| MUMPS | myra | provenance tree |
| Julia | - | matrices and images |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id lode
matrix 246        # the graph's slot IS the matrix layer
name Lode
needs camera
needs fs.read
needs fs.write
needs sys.read
needs journal
needs ledger.read
needs ledger.post
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

