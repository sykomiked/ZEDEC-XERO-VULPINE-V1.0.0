# Criteria — gust

| | |
|---|---|
| app | `gust` |
| matrix layer | 218 |
| icon group | L2 |
| wave spec | `wave18/218-gust.md` |

## Purpose

Plans children's outdoor play: where the child is and the family's safe areas (`place.here`, `place.list`, location shared only after `place.ask`), time left from the child's limits (`screen.check`, `screen.status`), the family's seats (`seats.list`); plans and the activity log in the Hoard (`files.read`, `files.write`). There is no weather source (as Weatherglass): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `screen.time`
- `sys.read`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | activity log |
| FORTRAN | trebago | wind and UV model |
| Ada | eve | time and distance limits |
| APL | kron | forecast grid |
| MUMPS | myra | family tree |
| Simula I | - | simulation |
| Pascal | pascal | step-by-step teaching |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id gust
matrix 218        # the graph's slot IS the matrix layer
name Gust
needs location
needs screen.time
needs sys.read
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

