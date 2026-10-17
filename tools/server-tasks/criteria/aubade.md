# Criteria — aubade

| | |
|---|---|
| app | `aubade` |
| matrix layer | 368 |
| icon group | L3 |
| wave spec | `wave31/368-aubade.md` |

## Purpose

Tracks daylight, UV and the sleep-wake rhythm to plan outdoor work and healthy light: the place (`place.here`), the clock (`sys.info`), logs in the Hoard (`files.read`, `files.write`), quiet hours (`screen.status`, `notes.list`). UV sensors have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `sys.read`
- `fs.read`
- `fs.write`
- `screen.time`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | shift pay |
| FORTRAN | trebago | solar angle model |
| Ada | eve | UV exposure limits |
| APL | kron | daylight grid |
| MUMPS | myra | worker profile tree |
| Simula I | - | simulation |
| LabVIEW | - | signals |
| Modula-2 | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id aubade
matrix 368        # the graph's slot IS the matrix layer
name Aubade
needs location
needs sys.read
needs fs.read
needs fs.write
needs screen.time
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

