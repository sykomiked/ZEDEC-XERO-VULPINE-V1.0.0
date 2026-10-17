# Criteria — galliard

| | |
|---|---|
| app | `galliard` |
| matrix layer | 390 |
| icon group | L3 |
| wave spec | `wave32/390-galliard.md` |

## Purpose

Trains martial arts, dance or movement with pose capture, drills, grading and injury-safe limits: sessions in the Hoard (`files.read`, `files.write`), the clock (`sys.info`), classes shown in a hall (`hall.load`, `hall.play`, `hall.status`). Pose capture from the camera has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `camera`
- `fs.read`
- `fs.write`
- `sys.read`
- `peers`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | dojo fees |
| FORTRAN | trebago | pose geometry |
| Ada | eve | joint angle limits |
| APL | kron | motion arrays |
| MUMPS | myra | student tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id galliard
matrix 390        # the graph's slot IS the matrix layer
name Galliard
needs camera
needs fs.read
needs fs.write
needs sys.read
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

