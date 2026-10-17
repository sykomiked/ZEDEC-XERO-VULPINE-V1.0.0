# Criteria — carrick

| | |
|---|---|
| app | `carrick` |
| matrix layer | 360 |
| icon group | L3 |
| wave spec | `wave30/360-carrick.md` |

## Purpose

Binds multiparty commitments into one living contract: text and obligations in the Hoard (`files.read`, `files.write`), every revision on the journal (`journal.query`, `journal.verify`), signatures (`web4.verify`), payments in the books (`ledger.post`, `ledger.hold`, `ledger.holds`, `ledger.release`), keys in escrow (`recovery.status`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `peers`
- `ledger.read`
- `ledger.post`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | obligation ledger |
| FORTRAN | trebago | penalty schedules |
| Ada | eve | limit clauses |
| APL | kron | obligation matrix |
| MUMPS | myra | party tree |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id carrick
matrix 360        # the graph's slot IS the matrix layer
name Carrick
needs fs.read
needs fs.write
needs journal
needs peers
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

