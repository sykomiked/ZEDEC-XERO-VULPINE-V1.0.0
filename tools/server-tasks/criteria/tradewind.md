# Criteria — tradewind

| | |
|---|---|
| app | `tradewind` |
| matrix layer | 211 |
| icon group | L2 |
| wave spec | `wave17/211-tradewind.md` |

## Purpose

plans routes for trips and shipments from saved places (`place.here`, `place.list`, `place.add`), weather and address lookups, kept in the Hoard (`files.read`, `files.write`). zxvd has no method for road network and address geocoding: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | waybills |
| FORTRAN | trebago | route optimisation |
| Ada | eve | load and hours limits |
| APL | kron | distance matrix |
| MUMPS | myra | stop hierarchy |
| K | kron | queries |
| Prolog | - | search |
| Simula 67 | - | simulation |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tradewind
matrix 211        # the graph's slot IS the matrix layer
name Tradewind
needs location
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

