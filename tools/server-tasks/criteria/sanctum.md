# Criteria — sanctum

| | |
|---|---|
| app | `sanctum` |
| matrix layer | 295 |
| icon group | L3 |
| wave spec | `wave24/295-sanctum.md` |

## Purpose

custody for valuables and digital assets: who holds what, guardians, withdrawal rules and signed audits (`ledger.accounts`, `ledger.balance`, `ledger.holds`, `recovery.status`, `journal.query`, `journal.verify`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `peers`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | custody ledger |
| FORTRAN | trebago | valuation model |
| Ada | eve | withdrawal limits |
| APL | kron | holdings grid |
| MUMPS | myra | beneficiary tree |
| SAS | - | reports |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id sanctum
matrix 295        # the graph's slot IS the matrix layer
name Sanctum
needs ledger.read
needs ledger.post
needs peers
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

