# Criteria — pyrometer

| | |
|---|---|
| app | `pyrometer` |
| matrix layer | 361 |
| icon group | L3 |
| wave spec | `wave30/361-pyrometer.md` |

## Purpose

Tracks home and building heating, fuel use and fire risk: readings (`sys.info`, `power.status`, `log.tail`, `journal.query`), inspections and bills in the Hoard (`files.read`, `files.write`), contacts rung on the person's request (`call.start`). Boiler, thermostat and smoke-alarm devices have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `journal`
- `fs.read`
- `fs.write`
- `peers`
- `location`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | fuel bills |
| FORTRAN | trebago | heat transfer model |
| Ada | eve | temperature cutoffs |
| APL | kron | room heat grid |
| MUMPS | myra | building tree |
| Bourne shell | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id pyrometer
matrix 361        # the graph's slot IS the matrix layer
name Pyrometer
needs sys.read
needs journal
needs fs.read
needs fs.write
needs peers
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

