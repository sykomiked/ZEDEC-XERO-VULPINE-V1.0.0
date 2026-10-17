# Criteria — trowel

| | |
|---|---|
| app | `trowel` |
| matrix layer | 184 |
| icon group | L2 |
| wave spec | `wave15/184-trowel.md` |

## Purpose

logs building upkeep: repairs, inspections, contractors and costs, in the Hoard (`files.list`, `files.read`, `files.write`, `sys.disk`) and the books (`ledger.entries`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `ledger.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | repair invoices |
| FORTRAN | trebago | decay model |
| Ada | eve | safety thresholds |
| APL | kron | room grid |
| MUMPS | myra | property tree |
| Erlang | - | fault handling |
| JCL | - | jobs |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id trowel
matrix 184        # the graph's slot IS the matrix layer
name Trowel
needs fs.read
needs fs.write
needs sys.read
needs ledger.read
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

