# Criteria — vintner

| | |
|---|---|
| app | `vintner` |
| matrix layer | 91 |
| icon group | L1 |
| wave spec | `wave1/091-vintner.md` |

## Purpose

vineyard lots, barrels, bottling and sales in the books (`ledger.open_account`, `ledger.post`, `ledger.entries`, `ledger.balance`); barrel lot labels from `grants.status`.

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| FORTRAN | trebago | exact numerics |
| COBOL | sutra | money and ledgers |
| C++ | cpp | money and ledgers |
| Forth | forth | small interpreters |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id vintner
matrix 91        # the graph's slot IS the matrix layer
name Vintner
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

