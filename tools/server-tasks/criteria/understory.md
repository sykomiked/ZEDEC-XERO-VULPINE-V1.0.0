# Criteria — understory

| | |
|---|---|
| app | `understory` |
| matrix layer | 93 |
| icon group | L1 |
| wave spec | `wave8/093-understory.md` |

## Purpose

energy, water and land budgets: energy from `power.status`, `power.top`, set with `power.budget` (feature `power`, off by default). No water or land method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| FORTRAN | trebago | exact numerics |
| COBOL | sutra | money and ledgers |
| C++ | cpp | money and ledgers |
| Ada | eve | limits |
| J | kron | limits |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id understory
matrix 93        # the graph's slot IS the matrix layer
name Understory
needs sys.read
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

