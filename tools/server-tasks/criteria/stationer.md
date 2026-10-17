# Criteria — stationer

| | |
|---|---|
| app | `stationer` |
| matrix layer | 202 |
| icon group | L2 |
| wave spec | `wave16/202-stationer.md` |

## Purpose

designs forms, letters and printed templates in the Hoard (`files.read`, `files.write`), prints (`print.status`, `print.submit`) and scans them (`scan.start`), and folds them into fill-and-sign packets.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `print`
- `scan`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | form registry |
| FORTRAN | trebago | layout math |
| Ada | eve | field rules |
| APL | kron | field grid |
| MUMPS | myra | form tree |
| PostScript | - | page layout |
| APT | - | tool paths |
| Lua | - | scripting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id stationer
matrix 202        # the graph's slot IS the matrix layer
name Stationer
needs fs.read
needs fs.write
needs print
needs scan
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

