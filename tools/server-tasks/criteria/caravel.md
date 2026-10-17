# Criteria — caravel

| | |
|---|---|
| app | `caravel` |
| matrix layer | 345 |
| icon group | L3 |
| wave spec | `wave29/345-caravel.md` |

## Purpose

Runs a trading stall or shop online and offline: stock and sales in the books (`ledger.post`, `ledger.balance`, `ledger.entries`), escrow (`ledger.hold`, `ledger.holds`, `ledger.release`), market listings (`net.offers`, `net.command` sell), parcels to buyers (`freight.pack`), receipts printed (`print.submit`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `peers`
- `net`
- `print`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | sales ledger |
| FORTRAN | trebago | price curves |
| Ada | eve | credit limits |
| APL | kron | inventory grid |
| MUMPS | myra | supplier tree |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id caravel
matrix 345        # the graph's slot IS the matrix layer
name Caravel
needs ledger.read
needs ledger.post
needs peers
needs net
needs print
needs fs.read
needs fs.write
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

