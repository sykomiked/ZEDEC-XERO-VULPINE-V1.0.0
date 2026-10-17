# Criteria — handfast

| | |
|---|---|
| app | `handfast` |
| matrix layer | 164 |
| icon group | L2 |
| wave spec | `wave13/164-handfast.md` |

## Purpose

drafts, negotiates and signs multi-party agreements in the Hoard (`files.read`, `files.write`) with every revision on the journal (`journal.query`, `journal.verify`) and signatures checked with `web4.verify`.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | obligations ledger |
| FORTRAN | trebago | payment schedules |
| Ada | eve | clause limits |
| APL | kron | party matrix |
| MUMPS | myra | clause tree |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id handfast
matrix 164        # the graph's slot IS the matrix layer
name Handfast
needs fs.read
needs fs.write
needs journal
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

