# Criteria — ninefold

| | |
|---|---|
| app | `ninefold` |
| matrix layer | 80 |
| icon group | L1 |
| wave spec | `wave1/080-ninefold.md` |

## Purpose

what the owner holds in each of the nine forms (finance/capital_forms.h), from `ledger.accounts` and `ledger.balance`. No method reports holdings by form: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `swarm`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | money and ledgers |
| FORTRAN | trebago | exact numerics |
| Ada | eve | limits |
| PostScript | - | page layout |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id ninefold
matrix 80        # the graph's slot IS the matrix layer
name Ninefold
needs ledger.read
needs swarm
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

