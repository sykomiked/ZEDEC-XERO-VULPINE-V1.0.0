# Criteria — neap

| | |
|---|---|
| app | `neap` |
| matrix layer | 234 |
| icon group | L2 |
| wave spec | `wave19/234-neap.md` |

## Purpose

Keeps double-entry books, reconciles accounts and prepares tax reports exactly: accounts and entries (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.check`), suspense holds (`ledger.hold`, `ledger.holds`, `ledger.release`), statements (`iso20022.render` camt.053), the jurisdiction (`jurisdiction.get`), reports saved and printed (`files.write`, `print.submit`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `fs.write`
- `print`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | general ledger |
| FORTRAN | trebago | depreciation model |
| Ada | eve | posting rules |
| APL | kron | account grid |
| MUMPS | myra | chart-of-accounts tree |
| RPG | - | reports |
| TRAC | - | text |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id neap
matrix 234        # the graph's slot IS the matrix layer
name Neap
needs ledger.read
needs ledger.post
needs fs.write
needs print
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

