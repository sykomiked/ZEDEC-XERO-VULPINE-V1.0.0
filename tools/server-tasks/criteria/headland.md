# Criteria — headland

| | |
|---|---|
| app | `headland` |
| matrix layer | 215 |
| icon group | L2 |
| wave spec | `wave17/215-headland.md` |

## Purpose

watches a machine or fleet for trouble (`sys.info`, `sys.disk`, `sys.net`, `log.tail`, `journal.query`) and guides the owner with prioritised alerts (`notes.list`). zxvd has no method for readings from other machines in the fleet: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `journal`
- `net`
- `notify`
- `peers`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | alert records |
| FORTRAN | trebago | trend detection |
| Ada | eve | alarm thresholds |
| APL | kron | metric grid |
| MUMPS | myra | fleet hierarchy |
| Go | go | concurrency |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id headland
matrix 215        # the graph's slot IS the matrix layer
name Headland
needs sys.read
needs journal
needs net
needs notify
needs peers
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

