# Criteria — spinnaker

| | |
|---|---|
| app | `spinnaker` |
| matrix layer | 366 |
| icon group | L3 |
| wave spec | `wave30/366-spinnaker.md` |

## Purpose

Plans and logs flights for small aircraft and drones: routes and places (`place.here`, `place.list`, `place.add`), weather from station records, checklists and maintenance in the Hoard (`files.read`, `files.write`), the link (`sys.net`). Airspace data and aircraft telemetry have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `sys.read`
- `fs.read`
- `fs.write`
- `net`
- `camera`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | landing fees |
| FORTRAN | trebago | flight dynamics |
| Ada | eve | weight and altitude limits |
| APL | kron | airspace grid |
| MUMPS | myra | airframe tree |
| IEC 61131-3 Structured Text | - | machine control |
| LISP | lisp | lists |
| Prolog | - | search |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id spinnaker
matrix 366        # the graph's slot IS the matrix layer
name Spinnaker
needs location
needs sys.read
needs fs.read
needs fs.write
needs net
needs camera
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

