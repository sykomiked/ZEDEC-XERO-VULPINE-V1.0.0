# Criteria — elevation

| | |
|---|---|
| app | `elevation` |
| matrix layer | 301 |
| icon group | L3 |
| wave spec | `wave25/301-elevation.md` |

## Purpose

designs buildings from sketch to construction drawings, with structure checks, materials and cost (`files.read`, `files.write`, `print.submit`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `print`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | cost estimates |
| FORTRAN | trebago | structural analysis |
| Ada | eve | building code limits |
| APL | kron | drawing grids |
| MUMPS | myra | building element tree |
| Julia | - | matrices and images |
| JCL | - | jobs |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id elevation
matrix 301        # the graph's slot IS the matrix layer
name Elevation
needs fs.read
needs fs.write
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

