# Criteria — lagoon

| | |
|---|---|
| app | `lagoon` |
| matrix layer | 270 |
| icon group | L2 |
| wave spec | `wave22/270-lagoon.md` |

## Purpose

Holds savings and guardianship plans for children until they come of age: accounts (`ledger.open_account`, `ledger.balance`, `ledger.entries`, `ledger.post`), escrow (`ledger.hold`, `ledger.holds`, `ledger.release`), the estate plan and guardians (`estate.status`, `recovery.status`), the child's seat (`seats.list`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `screen.time`
- `sys.read`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | trust accounts |
| FORTRAN | trebago | growth model |
| Ada | eve | withdrawal limits |
| APL | kron | beneficiary grid |
| MUMPS | myra | family tree |
| Simula 67 | - | simulation |
| BASIC | basic | step-by-step teaching |
| Oberon | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id lagoon
matrix 270        # the graph's slot IS the matrix layer
name Lagoon
needs ledger.read
needs ledger.post
needs screen.time
needs sys.read
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

