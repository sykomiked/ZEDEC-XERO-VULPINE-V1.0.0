# Criteria — courser

| | |
|---|---|
| app | `courser` |
| matrix layer | 188 |
| icon group | L2 |
| wave spec | `wave15/188-courser.md` |

## Purpose

plans and times workouts with music and logs results to the health record in the Hoard (`files.read`, `files.write`), time from `sys.info`.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | membership records |
| FORTRAN | trebago | pace analysis |
| Ada | eve | heart-rate limits |
| APL | kron | interval grid |
| MUMPS | myra | athlete tree |
| Simula I | - | simulation |
| LabVIEW | - | signals |
| Delphi | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id courser
matrix 188        # the graph's slot IS the matrix layer
name Courser
needs fs.read
needs fs.write
needs sys.read
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

