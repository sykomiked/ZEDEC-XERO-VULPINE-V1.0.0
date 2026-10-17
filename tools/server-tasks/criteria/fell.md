# Criteria — fell

| | |
|---|---|
| app | `fell` |
| matrix layer | 253 |
| icon group | L2 |
| wave spec | `wave21/253-fell.md` |

## Purpose

Coordinates search and rescue teams: maps and places (`place.here`, `place.list`, `place.add`), radio messages (`ehop.bands`, `ehop.seal`, `ehop.open`, `harmonic.encode`, `harmonic.decode`, `freight.pack`), the team room (`chat.post`, `chat.feed`, `pspace.post`), casualty records in the Hoard (`files.read`, `files.write`).

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
| COBOL | sutra | callout records |
| FORTRAN | trebago | search area model |
| Ada | eve | crew safety limits |
| APL | kron | sector grid |
| MUMPS | myra | casualty tree |
| Go | go | messaging |
| K | kron | queries |
| Prolog | - | search |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id fell
matrix 253        # the graph's slot IS the matrix layer
name Fell
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

