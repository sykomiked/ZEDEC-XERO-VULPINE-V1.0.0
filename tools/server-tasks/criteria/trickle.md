# Criteria — trickle

| | |
|---|---|
| app | `trickle` |
| matrix layer | 77 |
| icon group | L1 |
| wave spec | `wave8/077-trickle.md` |

## Purpose

standing streams and tithes the owner sets up, paid by `ledger.post` per period; read back with `ledger.entries`; `estate.stream`'s cumulative rule (floor(total x share), held-back units carried) is the pattern. No stream method exists: use only existing methods; list what's missing in your report.

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
| COBOL | sutra | money and ledgers |
| FORTRAN | trebago | exact numerics |
| Ada | eve | limits |
| Scala | - | dataflow |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id trickle
matrix 77        # the graph's slot IS the matrix layer
name Trickle
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

