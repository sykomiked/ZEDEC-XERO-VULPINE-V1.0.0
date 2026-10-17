# Criteria — atoll

| | |
|---|---|
| app | `atoll` |
| matrix layer | 258 |
| icon group | L2 |
| wave spec | `wave21/258-atoll.md` |

## Purpose

Keeps a marine log: tides, catches, reef health and sightings in the Hoard (`files.read`, `files.write`, `search.query`), the place (`place.here`, `place.list`), catch sales (`ledger.post`, `ledger.entries`).

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `fs.write`
- `search`
- `ledger.read`
- `ledger.post`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | catch and quota records |
| FORTRAN | trebago | tide model |
| Ada | eve | quota limits |
| APL | kron | zone grid |
| MUMPS | myra | species tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id atoll
matrix 258        # the graph's slot IS the matrix layer
name Atoll
needs location
needs fs.read
needs fs.write
needs search
needs ledger.read
needs ledger.post
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

