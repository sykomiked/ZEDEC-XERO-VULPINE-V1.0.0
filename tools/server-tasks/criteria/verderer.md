# Criteria — verderer

| | |
|---|---|
| app | `verderer` |
| matrix layer | 176 |
| icon group | L2 |
| wave spec | `wave14/176-verderer.md` |

## Purpose

logs wildlife sightings, trails and habitats with photos (`files.read`, `files.write`) and maps (`place.here`, `place.add`, `place.list`), searchable with `search.query`.

## Capabilities (needs — the exact registry names)

- `camera`
- `location`
- `fs.read`
- `fs.write`
- `search`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | survey records |
| FORTRAN | trebago | population model |
| Ada | eve | protected-area limits |
| APL | kron | sighting grid |
| MUMPS | myra | species tree |
| MATLAB | - | matrices and images |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id verderer
matrix 176        # the graph's slot IS the matrix layer
name Verderer
needs camera
needs location
needs fs.read
needs fs.write
needs search
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

