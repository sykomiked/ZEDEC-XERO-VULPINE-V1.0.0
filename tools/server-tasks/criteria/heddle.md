# Criteria — heddle

| | |
|---|---|
| app | `heddle` |
| matrix layer | 162 |
| icon group | L2 |
| wave spec | `wave13/162-heddle.md` |

## Purpose

designs woven and printed patterns and sends them to printers (`print.status`, `print.submit`, `print.job`) or looms as exact instructions, kept in the Hoard (`files.read`, `files.write`). zxvd has no method for loom output: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `print`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | yarn and cost sheet |
| FORTRAN | trebago | tension math |
| Ada | eve | machine limits |
| APL | kron | weave matrix |
| MUMPS | myra | pattern tree |
| APT | - | tool paths |
| PostScript | - | page layout |
| Perl | - | patterns |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id heddle
matrix 162        # the graph's slot IS the matrix layer
name Heddle
needs print
needs fs.read
needs fs.write
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

