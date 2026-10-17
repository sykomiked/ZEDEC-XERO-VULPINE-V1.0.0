# Criteria — furnace

| | |
|---|---|
| app | `furnace` |
| matrix layer | 199 |
| icon group | L2 |
| wave spec | `wave16/199-furnace.md` |

## Purpose

runs a home or village microgrid: solar output, storage and when to use power, from `power.status`, `power.top`, `power.budget` and `sys.info`. zxvd has no method for solar inverter, meter and battery bank readings: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `notify`
- `net`
- `peers`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | energy accounts |
| FORTRAN | trebago | solar model |
| Ada | eve | grid limits |
| APL | kron | load grid |
| MUMPS | myra | site tree |
| IEC 61131-3 Structured Text | - | machine control |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id furnace
matrix 199        # the graph's slot IS the matrix layer
name Furnace
needs sys.read
needs notify
needs net
needs peers
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

