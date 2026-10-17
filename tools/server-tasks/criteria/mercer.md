# Criteria — mercer

| | |
|---|---|
| app | `mercer` |
| matrix layer | 171 |
| icon group | L2 |
| wave spec | `wave14/171-mercer.md` |

## Purpose

runs a small shop: stock, prices, sales and receipts in the books (`ledger.post`, `ledger.balance`, `ledger.entries`) and the Hoard (`files.read`, `files.write`), receipts printed by `print.submit`, market listings from `net.offers`, the seller's card from `player.card`.

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `print`
- `net`
- `peers`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | sales ledger |
| FORTRAN | trebago | pricing model |
| Ada | eve | credit limits |
| APL | kron | stock grid |
| MUMPS | myra | product tree |
| LISP | lisp | lists |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id mercer
matrix 171        # the graph's slot IS the matrix layer
name Mercer
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs print
needs net
needs peers
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

