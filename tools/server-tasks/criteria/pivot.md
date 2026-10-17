# Criteria — pivot

| | |
|---|---|
| app | `pivot` |
| matrix layer | 136 |
| icon group | L1 |
| wave spec | `wave11/136-pivot.md` |

## Purpose

drive the machine with one or two switches (scanning) or the eyes (dwell to click). zxvd has no input-device or gaze method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `camera`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | safety rules |
| MUMPS | myra | records |
| R | - | statistics |
| OCaml | - | parsing |
| Mojo | - | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id pivot
matrix 136        # the graph's slot IS the matrix layer
name Pivot
needs camera
needs sys.read
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

