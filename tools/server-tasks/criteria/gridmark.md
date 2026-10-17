# Criteria — gridmark

| | |
|---|---|
| app | `gridmark` |
| matrix layer | 120 |
| icon group | L1 |
| wave spec | `wave10/120-gridmark.md` |

## Purpose

make QR codes and EAN-13 barcodes, save them as images (`files.write`), read them from images in the Hoard (`files.read`) or the camera. zxvd has no image-decode method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `camera`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| FORTRAN | trebago | exact numerics |
| APL | kron | arrays |
| OCaml | - | parsing |
| ALGOL 60 | algol60 | formulas |
| AWK | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id gridmark
matrix 120        # the graph's slot IS the matrix layer
name Gridmark
needs fs.read
needs fs.write
needs camera
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

