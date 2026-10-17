# Criteria — vigil

| | |
|---|---|
| app | `vigil` |
| matrix layer | 274 |
| icon group | L3 |
| wave spec | `wave23/274-vigil.md` |

## Purpose

watches servers, networks and agents through the night (`sys.info`, `sys.net`, `log.tail`, `journal.query`), waking the right person only when something truly needs a human (`notes.list`). zxvd has no method for readings from other machines (as Headland reports): use only existing methods; list what's missing in your report.

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
| COBOL | sutra | incident billing |
| FORTRAN | trebago | load forecasts |
| Ada | eve | alert thresholds |
| APL | kron | metric arrays |
| MUMPS | myra | system hierarchy |
| Go | go | messaging |
| Python | python | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id vigil
matrix 274        # the graph's slot IS the matrix layer
name Vigil
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

