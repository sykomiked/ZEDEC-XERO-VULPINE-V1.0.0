# Criteria — magpie

| | |
|---|---|
| app | `magpie` |
| matrix layer | 119 |
| icon group | L1 |
| wave spec | `wave10/119-magpie.md` |

## Purpose

the last copies, to paste again, pin or clear. Kept in memory only. zxvd has no clipboard method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| FORTRAN | trebago | exact numerics |
| APL | kron | arrays |
| dBase | - | records |
| TRAC | - | text |
| ALGOL 58 | - | formulas |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id magpie
matrix 119        # the graph's slot IS the matrix layer
name Magpie
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

