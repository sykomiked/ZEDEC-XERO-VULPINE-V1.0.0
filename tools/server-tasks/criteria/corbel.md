# Criteria — corbel

| | |
|---|---|
| app | `corbel` |
| matrix layer | 338 |
| icon group | L3 |
| wave spec | `wave28/338-corbel.md` |

## Purpose

guides visitors through heritage sites: stories, audio tours, conservation records and ticketing (`place.list`, `files.read`, `files.write`, `hall.load`, `hall.play`).

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `fs.write`
- `peers`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | ticket revenue |
| FORTRAN | trebago | visitor wear model |
| Ada | eve | visitor capacity limits |
| APL | kron | site grid |
| MUMPS | myra | artefact hierarchy |
| Mojo | - | AI |
| LabVIEW | - | signals |
| Pascal | pascal | step-by-step teaching |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id corbel
matrix 338        # the graph's slot IS the matrix layer
name Corbel
needs location
needs fs.read
needs fs.write
needs peers
needs net
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

