# Criteria — perigee

| | |
|---|---|
| app | `perigee` |
| matrix layer | 286 |
| icon group | L3 |
| wave spec | `wave24/286-perigee.md` |

## Purpose

plans drone deliveries and inspections within declared airspace, weather and battery limits, from places (`place.here`, `place.list`) and station data (`devmesh.devices`), with plans in the Hoard (`files.read`, `files.write`). zxvd has no method for drone control and telemetry (as Tiller reports), airspace notices: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `sys.read`
- `net`
- `peers`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | delivery charges |
| FORTRAN | trebago | flight path model |
| Ada | eve | altitude and battery limits |
| APL | kron | airspace grid |
| MUMPS | myra | fleet records |
| Ladder logic | - | machine control |
| Simula I | - | simulation |
| Delphi | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id perigee
matrix 286        # the graph's slot IS the matrix layer
name Perigee
needs location
needs sys.read
needs net
needs peers
needs fs.read
needs fs.write
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

