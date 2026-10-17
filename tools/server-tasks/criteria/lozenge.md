# Criteria — lozenge

| | |
|---|---|
| app | `lozenge` |
| matrix layer | 281 |
| icon group | L3 |
| wave spec | `wave23/281-lozenge.md` |

## Purpose

tracks hives, pollination contracts with growers and honey sales for beekeepers and their co-op (`ledger.entries`, `ledger.post`, `net.offers`, `files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `net`
- `peers`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | pollination invoices |
| FORTRAN | trebago | colony strength model |
| Ada | eve | pesticide distance limits |
| APL | kron | hive grid |
| MUMPS | myra | colony lineage tree |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id lozenge
matrix 281        # the graph's slot IS the matrix layer
name Lozenge
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
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

