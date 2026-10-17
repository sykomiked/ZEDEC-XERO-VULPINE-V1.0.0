# Criteria — damask

| | |
|---|---|
| app | `damask` |
| matrix layer | 355 |
| icon group | L3 |
| wave spec | `wave29/355-damask.md` |

## Purpose

Runs a restaurant kitchen: menus, recipes, orders, allergens and stock in the Hoard (`files.read`, `files.write`, `search.query`), supplier payments and sales in the books (`ledger.post`, `ledger.entries`, `ledger.balance`), tickets printed (`print.submit`), shift times (`sys.info`). Fridge sensors have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `ledger.read`
- `ledger.post`
- `print`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | supplier payments |
| FORTRAN | trebago | portion costing |
| Ada | eve | fridge temperature limits |
| APL | kron | menu grid |
| MUMPS | myra | allergen tree |
| Laning and Zierler | - | formulas |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id damask
matrix 355        # the graph's slot IS the matrix layer
name Damask
needs fs.read
needs fs.write
needs search
needs ledger.read
needs ledger.post
needs print
needs sys.read
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

