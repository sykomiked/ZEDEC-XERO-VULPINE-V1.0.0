# Criteria — viaduct

| | |
|---|---|
| app | `viaduct` |
| matrix layer | 327 |
| icon group | L3 |
| wave spec | `wave27/327-viaduct.md` |

## Purpose

settles payments between currencies, co-ops and networks with fair rates and clear records (`ledger.accounts`, `ledger.post`, `ledger.hold`, `ledger.release`, `netting.open`, `netting.positions`, `netting.close`, `swap.quote`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | settlement ledger |
| FORTRAN | trebago | exchange rate model |
| Ada | eve | transfer limits |
| APL | kron | currency grid |
| MUMPS | myra | counterparty tree |
| Erlang | - | messaging |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id viaduct
matrix 327        # the graph's slot IS the matrix layer
name Viaduct
needs ledger.read
needs ledger.post
needs peers
needs net
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

