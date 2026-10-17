# Criteria — tephra

| | |
|---|---|
| app | `tephra` |
| matrix layer | 328 |
| icon group | L3 |
| wave spec | `wave27/328-tephra.md` |

## Purpose

monitors volcanoes, earthquakes and landslides, turning sensor data into hazard maps and evacuation advice (`journal.query`, `log.tail`, `place.here`, `net.peers`, `pspace.post`). zxvd has no method for hazard sensor feeds: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `journal`
- `location`
- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | response budget |
| FORTRAN | trebago | seismic models |
| Ada | eve | evacuation thresholds |
| APL | kron | sensor arrays |
| MUMPS | myra | zone hierarchy |
| JOVIAL | - | machine control |
| Clojure | - | dataflow |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tephra
matrix 328        # the graph's slot IS the matrix layer
name Tephra
needs journal
needs location
needs peers
needs net
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

