# Criteria — bearing

| | |
|---|---|
| app | `bearing` |
| matrix layer | 401 |
| icon group | L3 |
| wave spec | `wave33/401-bearing.md` |

## Purpose

Plans multi-stop journeys across land and sea: places (`place.here`, `place.list`, `place.add`), tickets and papers in the Hoard (`files.read`, `files.write`), parcels and messages (`freight.pack`, `net.peers`), ticket payments (`ledger.post`). Timetables and live delays have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `fs.write`
- `peers`
- `net`
- `ledger.read`
- `ledger.post`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | ticket payments |
| FORTRAN | trebago | route timing |
| Ada | eve | connection limits |
| APL | kron | timetable grid |
| MUMPS | myra | itinerary tree |
| Prolog | - | search |
| Simula 67 | - | simulation |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id bearing
matrix 401        # the graph's slot IS the matrix layer
name Bearing
needs location
needs fs.read
needs fs.write
needs peers
needs net
needs ledger.read
needs ledger.post
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

