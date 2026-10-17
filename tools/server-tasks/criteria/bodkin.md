# Criteria — bodkin

| | |
|---|---|
| app | `bodkin` |
| matrix layer | 183 |
| icon group | L2 |
| wave spec | `wave15/183-bodkin.md` |

## Purpose

turns body measurements into fitted sewing patterns and cut sheets in the Hoard (`files.read`, `files.write`), printed with `print.submit`.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `print`
- `ledger.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | order book |
| FORTRAN | trebago | fit geometry |
| Ada | eve | size limits |
| APL | kron | measurement grid |
| MUMPS | myra | garment tree |
| APT | - | tool paths |
| SNOBOL | - | patterns |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id bodkin
matrix 183        # the graph's slot IS the matrix layer
name Bodkin
needs fs.read
needs fs.write
needs print
needs ledger.read
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

