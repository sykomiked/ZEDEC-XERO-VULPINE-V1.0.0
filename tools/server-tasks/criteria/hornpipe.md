# Criteria — hornpipe

| | |
|---|---|
| app | `hornpipe` |
| matrix layer | 384 |
| icon group | L3 |
| wave spec | `wave32/384-hornpipe.md` |

## Purpose

Coaches swimming and water sports: drills and lap times in the Hoard (`files.read`, `files.write`), the clock (`sys.info`), open-water check-ins (`place.here`, `chat.post`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `location`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | club fees |
| FORTRAN | trebago | stroke analysis |
| Ada | eve | cold and distance limits |
| APL | kron | lap arrays |
| MUMPS | myra | swimmer tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id hornpipe
matrix 384        # the graph's slot IS the matrix layer
name Hornpipe
needs fs.read
needs fs.write
needs sys.read
needs location
needs peers
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

