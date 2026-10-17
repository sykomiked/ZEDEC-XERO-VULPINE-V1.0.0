# Criteria — burin

| | |
|---|---|
| app | `burin` |
| matrix layer | 54 |
| icon group | L1 |
| wave spec | `wave7/054-burin.md` |

## Purpose

drawings as files (`files.read`, `files.write`), printed (`print.submit`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `print`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| FORTRAN | trebago | exact numerics |
| APT | - | tool paths |
| Logo | logo | geometry |
| LabVIEW | - | signals |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id burin
matrix 54        # the graph's slot IS the matrix layer
name Burin
needs fs.read
needs fs.write
needs print
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

