# Criteria — rainshadow

| | |
|---|---|
| app | `rainshadow` |
| matrix layer | 261 |
| icon group | L2 |
| wave spec | `wave21/261-rainshadow.md` |

## Purpose

Plans sky watching: eclipses and transits, where and when, from the place (`place.here`, `place.ask`) and the clock (`sys.info`); photo plans and room (`files.write`, `sys.disk`).

## Capabilities (needs — the exact registry names)

- `location`
- `sys.read`
- `fs.write`
- `camera`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | observation log |
| FORTRAN | trebago | orbital model |
| Ada | eve | eye safety limits |
| APL | kron | ephemeris grid |
| MUMPS | myra | event tree |
| Mojo | - | AI |
| Simula I | - | simulation |
| Pascal | pascal | step-by-step teaching |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id rainshadow
matrix 261        # the graph's slot IS the matrix layer
name Rainshadow
needs location
needs sys.read
needs fs.write
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

