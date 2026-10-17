# Criteria — cirque

| | |
|---|---|
| app | `cirque` |
| matrix layer | 227 |
| icon group | L2 |
| wave spec | `wave18/227-cirque.md` |

## Purpose

Draws buildings from plan to 3D, checks measurements and produces drawing sets: projects in the Hoard (`files.read`, `files.write`), sets printed (`print.submit`, `print.status`).

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
| COBOL | sutra | project records |
| FORTRAN | trebago | structural loads |
| Ada | eve | code limits |
| APL | kron | dimension grid |
| MUMPS | myra | building element tree |
| Logo | logo | geometry |
| Julia | - | matrices and images |
| Bash | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cirque
matrix 227        # the graph's slot IS the matrix layer
name Cirque
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

