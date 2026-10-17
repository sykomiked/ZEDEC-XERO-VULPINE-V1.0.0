# Criteria — tallystick

| | |
|---|---|
| app | `tallystick` |
| matrix layer | 78 |
| icon group | L1 |
| wave spec | `wave4/078-tallystick.md` |

## Purpose

make invoices and receipts, record payments, render statements (`ledger.post`, `ledger.entries`, `iso20022.render` camt.053), save and print them through the Banker (`files.write`, `print.submit`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `print`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | money and ledgers |
| FORTRAN | trebago | exact numerics |
| Ada | eve | limits |
| Python | python | AI |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tallystick
matrix 78        # the graph's slot IS the matrix layer
name Tallystick
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs print
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

