# Criteria — tiller

| | |
|---|---|
| app | `tiller` |
| matrix layer | 175 |
| icon group | L2 |
| wave spec | `wave14/175-tiller.md` |

## Purpose

one control panel for vehicles, drones and machines with hard safety limits, reading link rates (`sys.net`) and places (`place.here`, `place.list`). zxvd has no method for any vehicle, drone or machine telemetry and control method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `sys.read`
- `net`
- `camera`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | trip log |
| FORTRAN | trebago | flight dynamics |
| Ada | eve | control envelope |
| APL | kron | telemetry arrays |
| MUMPS | myra | craft tree |
| Ladder logic | - | machine control |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tiller
matrix 175        # the graph's slot IS the matrix layer
name Tiller
needs location
needs sys.read
needs net
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

