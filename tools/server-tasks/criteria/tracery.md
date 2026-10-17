# Criteria — tracery

| | |
|---|---|
| app | `tracery` |
| matrix layer | 275 |
| icon group | L3 |
| wave spec | `wave23/275-tracery.md` |

## Purpose

records wildlife sightings, herd counts and habitat health across a reserve (`place.here`, `place.list`, `place.add`, `files.read`, `files.write`, `search.query`) for rangers and landowners.

## Capabilities (needs — the exact registry names)

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
| COBOL | sutra | permit fees |
| FORTRAN | trebago | population model |
| Ada | eve | cull and quota limits |
| APL | kron | sighting grid |
| MUMPS | myra | species tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tracery
matrix 275        # the graph's slot IS the matrix layer
name Tracery
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

