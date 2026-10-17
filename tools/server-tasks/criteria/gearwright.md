# Criteria — gearwright

| | |
|---|---|
| app | `gearwright` |
| matrix layer | 201 |
| icon group | L2 |
| wave spec | `wave16/201-gearwright.md` |

## Purpose

programs and supervises robots and actuators with scheduled routines and safety stops, machine facts from `sys.info`. zxvd has no method for actuator control and position readings: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `notify`
- `peers`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | production records |
| FORTRAN | trebago | kinematics |
| Ada | eve | motion limits |
| APL | kron | joint arrays |
| MUMPS | myra | machine tree |
| JOVIAL | - | machine control |
| JCL | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id gearwright
matrix 201        # the graph's slot IS the matrix layer
name Gearwright
needs sys.read
needs notify
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

