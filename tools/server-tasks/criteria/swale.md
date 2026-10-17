# Criteria — swale

| | |
|---|---|
| app | `swale` |
| matrix layer | 226 |
| icon group | L2 |
| wave spec | `wave18/226-swale.md` |

## Purpose

Runs a small bakery: recipes and orders in the Hoard (`files.read`, `files.write`), sales and receipts in the books (`ledger.accounts`, `ledger.entries`, `ledger.post`), statements (`iso20022.render`), receipts printed (`print.submit`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `print`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | orders and receipts |
| FORTRAN | trebago | dough and proofing model |
| Ada | eve | allergen limits |
| APL | kron | batch grid |
| MUMPS | myra | recipe tree |
| Autocode | - | formulas |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id swale
matrix 226        # the graph's slot IS the matrix layer
name Swale
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
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

