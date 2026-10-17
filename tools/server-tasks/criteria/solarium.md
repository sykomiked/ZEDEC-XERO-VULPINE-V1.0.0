# Criteria — solarium

| | |
|---|---|
| app | `solarium` |
| matrix layer | 292 |
| icon group | L3 |
| wave spec | `wave24/292-solarium.md` |

## Purpose

looks after a garden or greenhouse: planting plans, watering, soil and light sensors, and harvest logs (`files.read`, `files.write`, `place.here`). zxvd has no method for soil and light sensor readings, valve control: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `location`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | seed and produce accounts |
| FORTRAN | trebago | evapotranspiration model |
| Ada | eve | watering limits |
| APL | kron | bed sensor grid |
| MUMPS | myra | plant lineage |
| IEC 61131-3 Structured Text | - | machine control |
| Simula I | - | simulation |
| Modula-2 | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id solarium
matrix 292        # the graph's slot IS the matrix layer
name Solarium
needs fs.read
needs fs.write
needs location
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

