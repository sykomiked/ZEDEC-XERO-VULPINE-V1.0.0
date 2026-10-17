# Criteria — copse

| | |
|---|---|
| app | `copse` |
| matrix layer | 235 |
| icon group | L2 |
| wave spec | `wave19/235-copse.md` |

## Purpose

Plans woodworking: designs, cut lists, joints, materials and cost, kept in the Hoard (`files.read`, `files.write`) and printed (`print.submit`).

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
| COBOL | sutra | material costs |
| FORTRAN | trebago | board yield |
| Ada | eve | tolerance limits |
| APL | kron | cut grid |
| MUMPS | myra | assembly tree |
| APT | - | tool paths |
| LISP | lisp | lists |
| Simula 67 | - | simulation |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id copse
matrix 235        # the graph's slot IS the matrix layer
name Copse
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

