# Criteria — armillary

| | |
|---|---|
| app | `armillary` |
| matrix layer | 349 |
| icon group | L3 |
| wave spec | `wave29/349-armillary.md` |

## Purpose

Calibrates and maintains precision machines: readings from this machine (`sys.info`, `sys.disk`, `power.status`), service logs and plans in the Hoard (`files.read`, `files.write`), service times from the shared clock. Readings from other machines and their instruments have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `fs.write`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | service contracts |
| FORTRAN | trebago | calibration curves |
| Ada | eve | torque and drift limits |
| APL | kron | sensor matrices |
| MUMPS | myra | assembly tree |
| IEC 61131-3 Structured Text | - | machine control |
| Simula 67 | - | simulation |
| Scala | - | dataflow |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id armillary
matrix 349        # the graph's slot IS the matrix layer
name Armillary
needs sys.read
needs fs.read
needs fs.write
needs peers
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

