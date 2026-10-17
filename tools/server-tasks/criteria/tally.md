# Criteria — tally

| | |
|---|---|
| app | `tally` |
| matrix layer | 109 |
| icon group | L1 |
| wave spec | `wave2/109-tally.md` |

## Purpose

integer and exact rational arithmetic (kernel/src/rational: reduced num/den, overflow refused), units, currency in minor units. Currencies the owner enabled come from `ledger.accounts`. zxvd has no exchange-rate method: use only existing methods; list what's missing in your report. Conversions are shown, never posted.

## Capabilities (needs — the exact registry names)

- `ledger.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| FORTRAN | trebago | exact numerics |
| COBOL | sutra | money and ledgers |
| APL | kron | arrays |
| ALGOL 60 | algol60 | formulas |
| TRAC | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tally
matrix 109        # the graph's slot IS the matrix layer
name Tally
needs ledger.read
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

