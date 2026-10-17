# Criteria — landfall

| | |
|---|---|
| app | `landfall` |
| matrix layer | 356 |
| icon group | L3 |
| wave spec | `wave30/356-landfall.md` |

## Purpose

Plans and records expeditions: routes and offline navigation (`place.here`, `place.list`, `place.add`), gear lists in the Hoard (`files.read`, `files.write`), check-ins over the mesh or radio (`chat.post`, `ehop.bands`, `ehop.seal`, `harmonic.encode`), alerts to contacts if a return time is missed (on the person's prior setup).

## Capabilities (needs — the exact registry names)

- `location`
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
| COBOL | sutra | permit fees |
| FORTRAN | trebago | route energy model |
| Ada | eve | weather and turnaround limits |
| APL | kron | waypoint arrays |
| MUMPS | myra | team roster tree |
| Gleam | - | messaging |
| Prolog | - | search |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id landfall
matrix 356        # the graph's slot IS the matrix layer
name Landfall
needs location
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

