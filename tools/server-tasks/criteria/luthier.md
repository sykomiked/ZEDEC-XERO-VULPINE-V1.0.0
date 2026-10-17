# Criteria — luthier

| | |
|---|---|
| app | `luthier` |
| matrix layer | 157 |
| icon group | L2 |
| wave spec | `wave13/157-luthier.md` |

## Purpose

composes, notates and records music, from instrument loops to finished scores, kept in the Hoard (`files.read`, `files.write`). zxvd has no method for audio capture and playback devices (as Birdsong reports): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `mic`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | royalty splits |
| FORTRAN | trebago | tuning math |
| Ada | eve | level limits |
| APL | kron | score arrays |
| MUMPS | myra | piece hierarchy |
| LabVIEW | - | signals |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id luthier
matrix 157        # the graph's slot IS the matrix layer
name Luthier
needs fs.read
needs fs.write
needs mic
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

