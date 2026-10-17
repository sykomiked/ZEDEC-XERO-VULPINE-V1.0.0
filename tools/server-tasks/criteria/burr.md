# Criteria — burr

| | |
|---|---|
| app | `burr` |
| matrix layer | 118 |
| icon group | L1 |
| wave spec | `wave10/118-burr.md` |

## Purpose

sticky notes on the desktop, each a text file in a Notes folder of the Hoard (`files.list`, `files.read`, `files.write`, `files.mkdir`, `files.trash`); a removed note goes to the Trash and can be put back.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| FORTRAN | trebago | exact numerics |
| APL | kron | arrays |
| SNOBOL | - | text |
| Short Code | - | formulas |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id burr
matrix 118        # the graph's slot IS the matrix layer
name Burr
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

