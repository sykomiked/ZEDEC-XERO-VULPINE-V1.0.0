# Criteria — manometer

| | |
|---|---|
| app | `manometer` |
| matrix layer | 395 |
| icon group | L3 |
| wave spec | `wave33/395-manometer.md` |

## Purpose

Supervises a power plant or microgrid: core readings, interlocks, operator logs and regulator reports, from this machine (`sys.info`, `power.status`, `power.top`, `log.tail`, `journal.query`), the perimeter (`features.get`), logs and reports in the Hoard (`files.read`, `files.write`). Plant telemetry and interlock control have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `journal`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | generation billing |
| FORTRAN | trebago | thermal hydraulics |
| Ada | eve | interlock limits |
| APL | kron | sensor grid |
| MUMPS | myra | plant component tree |
| IEC 61131-3 Structured Text | - | machine control |
| SAS | - | reports |
| Simula I | - | simulation |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id manometer
matrix 395        # the graph's slot IS the matrix layer
name Manometer
needs sys.read
needs journal
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

