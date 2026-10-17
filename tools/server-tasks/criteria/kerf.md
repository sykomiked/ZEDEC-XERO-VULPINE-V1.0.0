# Criteria — kerf

| | |
|---|---|
| app | `kerf` |
| matrix layer | 290 |
| icon group | L3 |
| wave spec | `wave24/290-kerf.md` |

## Purpose

runs a small workshop or factory: job orders, materials, machine time, quality checks and shipping, from the Hoard (`files.read`, `files.write`), the books (`ledger.entries`, `ledger.post`) and printed papers (`print.submit`). zxvd has no method for machine state readings: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `print`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | job costing |
| FORTRAN | trebago | tolerance calculations |
| Ada | eve | machine safety limits |
| APL | kron | production schedule grid |
| MUMPS | myra | bill-of-materials tree |
| Bourne shell | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id kerf
matrix 290        # the graph's slot IS the matrix layer
name Kerf
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs print
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

