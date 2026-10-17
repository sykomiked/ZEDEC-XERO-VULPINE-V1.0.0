# Criteria — crowsnest

| | |
|---|---|
| app | `crowsnest` |
| matrix layer | 358 |
| icon group | L3 |
| wave spec | `wave30/358-crowsnest.md` |

## Purpose

Watches cameras and sensors across sites, spots unusual activity and logs signed evidence: events (`log.tail`, `journal.query`, `journal.verify`), files (`files.stat`, `search.query`), places (`place.list`), the perimeter (`features.get`, `sys.net`). Camera streams from other sites have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `journal`
- `sys.read`
- `search`
- `fs.read`
- `location`
- `camera`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | incident billing |
| FORTRAN | trebago | motion detection |
| Ada | eve | privacy masking limits |
| APL | kron | frame arrays |
| MUMPS | myra | site tree |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |
| JOVIAL | - | machine control |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id crowsnest
matrix 358        # the graph's slot IS the matrix layer
name Crowsnest
needs journal
needs sys.read
needs search
needs fs.read
needs location
needs camera
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

