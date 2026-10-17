# Criteria — mummer

| | |
|---|---|
| app | `mummer` |
| matrix layer | 182 |
| icon group | L2 |
| wave spec | `wave15/182-mummer.md` |

## Purpose

produces shows: scripts, cues, music and slides in the Hoard (`files.read`, `files.write`) and the audience event as a watch hall (`pspace.create`, `hall.load`, `hall.play`, `hall.pause`, `hall.status`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | ticket ledger |
| FORTRAN | trebago | cue timing |
| Ada | eve | stage safety limits |
| APL | kron | cue grid |
| MUMPS | myra | production tree |
| Groovy | - | scripting |
| OCaml | - | parsing |
| LabVIEW | - | signals |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id mummer
matrix 182        # the graph's slot IS the matrix layer
name Mummer
needs fs.read
needs fs.write
needs peers
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

