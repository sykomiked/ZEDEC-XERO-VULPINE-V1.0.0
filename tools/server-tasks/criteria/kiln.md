# Criteria — kiln

| | |
|---|---|
| app | `kiln` |
| matrix layer | 186 |
| icon group | L2 |
| wave spec | `wave15/186-kiln.md` |

## Purpose

queues 3D models from the Hoard (`files.read`) for printing, firing or fabrication (`print.status`, `print.submit`, `print.job`, `print.cancel`) and tracks each piece. zxvd has no method for 3D printer and kiln control: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `print`
- `fs.read`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | job costing |
| FORTRAN | trebago | heat curves |
| Ada | eve | temperature limits |
| APL | kron | layer arrays |
| MUMPS | myra | batch tree |
| APT | - | tool paths |
| MATLAB | - | matrices and images |
| Bash | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id kiln
matrix 186        # the graph's slot IS the matrix layer
name Kiln
needs print
needs fs.read
needs sys.read
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

