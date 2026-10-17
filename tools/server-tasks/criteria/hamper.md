# Criteria — hamper

| | |
|---|---|
| app | `hamper` |
| matrix layer | 112 |
| icon group | L1 |
| wave spec | `wave9/112-hamper.md` |

## Purpose

open and make compressed archives in the Hoard (`files.list`, `files.stat`, `files.read`, `files.write`, `files.mkdir`), never over an existing item. zxvd has no archive codec method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| FORTRAN | trebago | exact numerics |
| APL | kron | arrays |
| dBase | - | records |
| Autocode | - | formulas |
| AWK | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id hamper
matrix 112        # the graph's slot IS the matrix layer
name Hamper
needs fs.read
needs fs.write
needs sys.read
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

