# Criteria — capstan

| | |
|---|---|
| app | `capstan` |
| matrix layer | 359 |
| icon group | L3 |
| wave spec | `wave30/359-capstan.md` |

## Purpose

Monitors and dispatches wind and water turbines: readings from this machine (`power.status`, `power.top`), station records (`place.list`), maintenance plans and output in the Hoard (`files.read`, `files.write`), grid payments in the books (`ledger.post`, `ledger.entries`). Turbine telemetry has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `location`
- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | grid payments |
| FORTRAN | trebago | blade aerodynamics |
| Ada | eve | rotor speed limits |
| APL | kron | farm output grid |
| MUMPS | myra | turbine part tree |
| Ladder logic | - | machine control |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id capstan
matrix 359        # the graph's slot IS the matrix layer
name Capstan
needs sys.read
needs location
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
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

