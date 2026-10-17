# Criteria — pinhole

| | |
|---|---|
| app | `pinhole` |
| matrix layer | 58 |
| icon group | L1 |
| wave spec | `wave7/058-pinhole.md` |

## Purpose

photos saved in the Hoard (`files.write`), room left from `sys.disk`, a place tag only after the owner's answer (`place.ask`). Capturing from the camera has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `camera`
- `location`
- `fs.write`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| FORTRAN | trebago | exact numerics |
| LabVIEW | - | signals |
| Julia | - | matrices and images |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id pinhole
matrix 58        # the graph's slot IS the matrix layer
name Pinhole
needs camera
needs location
needs fs.write
needs sys.read
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

