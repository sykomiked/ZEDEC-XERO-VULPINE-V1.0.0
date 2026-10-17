# Criteria — piccolo

| | |
|---|---|
| app | `piccolo` |
| matrix layer | 385 |
| icon group | L3 |
| wave spec | `wave32/385-piccolo.md` |

## Purpose

Plans pollinator-friendly gardens and tracks bees, birds and blooms with neighbours: beds and sightings in the Hoard (`files.read`, `files.write`, `search.query`), places (`place.here`, `place.list`), the neighbours' circle (`social.post`, `social.feed`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `location`
- `peers`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | seed swap credits |
| FORTRAN | trebago | bloom timing |
| Ada | eve | pesticide limits |
| APL | kron | bloom calendar grid |
| MUMPS | myra | species tree |
| APT | - | tool paths |
| Simula 67 | - | simulation |
| Modula-2 | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id piccolo
matrix 385        # the graph's slot IS the matrix layer
name Piccolo
needs fs.read
needs fs.write
needs search
needs location
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

