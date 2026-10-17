# Criteria — argonaut

| | |
|---|---|
| app | `argonaut` |
| matrix layer | 367 |
| icon group | L3 |
| wave spec | `wave30/367-argonaut.md` |

## Purpose

Grows savings in chambered goals that compound over time, with locked portions: accounts (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.open_account`, `ledger.post`), holds for locked chambers (`ledger.hold`, `ledger.holds`, `ledger.release`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | savings ledger |
| FORTRAN | trebago | integer compound interest |
| Ada | eve | withdrawal limits |
| APL | kron | goal grid |
| MUMPS | myra | household tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id argonaut
matrix 367        # the graph's slot IS the matrix layer
name Argonaut
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

