# Criteria — binnacle

| | |
|---|---|
| app | `binnacle` |
| matrix layer | 147 |
| icon group | L2 |
| wave spec | `wave12/147-binnacle.md` |

## Purpose

position, heading and routes worked out offline from star sights, the clock (`sys.info`) and saved places (`place.here`, `place.list`, `place.add`), with no outside service. zxvd has no method for star-sight entry from an instrument, chart data: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | voyage log |
| FORTRAN | trebago | fix calculation |
| Ada | eve | safe-heading bounds |
| APL | kron | bearing arrays |
| MUMPS | myra | waypoint tree |
| Logo | logo | geometry |
| Prolog | - | search |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id binnacle
matrix 147        # the graph's slot IS the matrix layer
name Binnacle
needs location
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

