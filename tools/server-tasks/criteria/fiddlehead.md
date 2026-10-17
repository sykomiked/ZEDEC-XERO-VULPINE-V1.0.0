# Criteria — fiddlehead

| | |
|---|---|
| app | `fiddlehead` |
| matrix layer | 57 |
| icon group | L1 |
| wave spec | `wave7/057-fiddlehead.md` |

## Purpose

instruments, patterns and loops as files (`files.read`, `files.write`). Audio in and out has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `mic`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| FORTRAN | trebago | exact numerics |
| LabVIEW | - | signals |
| MATLAB | - | matrices and images |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id fiddlehead
matrix 57        # the graph's slot IS the matrix layer
name Fiddlehead
needs fs.read
needs fs.write
needs mic
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

