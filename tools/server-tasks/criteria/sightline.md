# Criteria — sightline

| | |
|---|---|
| app | `sightline` |
| matrix layer | 298 |
| icon group | L3 |
| wave spec | `wave25/298-sightline.md` |

## Purpose

plans and patrols trails and wild land: routes, hazards, check-ins and rescue handoffs (`place.here`, `place.list`, `place.add`, `chat.post`, `pspace.post`, `files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `location`
- `peers`
- `net`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | permit fees |
| FORTRAN | trebago | terrain and fire model |
| Ada | eve | patrol time limits |
| APL | kron | trail grid |
| MUMPS | myra | visitor records |
| Prolog | - | search |
| Simula 67 | - | simulation |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id sightline
matrix 298        # the graph's slot IS the matrix layer
name Sightline
needs location
needs peers
needs net
needs fs.read
needs fs.write
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

