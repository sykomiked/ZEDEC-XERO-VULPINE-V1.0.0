# Criteria — astrolabe

| | |
|---|---|
| app | `astrolabe` |
| matrix layer | 160 |
| icon group | L2 |
| wave spec | `wave13/160-astrolabe.md` |

## Purpose

plans observing sessions, logs sightings and predicts sky events from local time (`sys.info`) and place (`place.here`), with photos saved to the Hoard (`files.write`). zxvd has no method for a weather source (as Weatherglass reports): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `sys.read`
- `camera`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | observation log |
| FORTRAN | trebago | orbit computation |
| Ada | eve | exposure limits |
| APL | kron | star catalogue arrays |
| MUMPS | myra | object hierarchy |
| Python | python | AI |
| Simula I | - | simulation |
| Modula-2 | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id astrolabe
matrix 160        # the graph's slot IS the matrix layer
name Astrolabe
needs location
needs sys.read
needs camera
needs fs.write
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

