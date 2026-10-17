# Criteria — damper

| | |
|---|---|
| app | `damper` |
| matrix layer | 174 |
| icon group | L2 |
| wave spec | `wave14/174-damper.md` |

## Purpose

watches heat, smoke and power readings (`sys.info`, `power.status`, `log.tail`, `journal.query`) and coordinates alarms and evacuation contacts (`phone.list`, `call.start` on the person's request, `place.here`). zxvd has no method for home heat and smoke sensor readings, notice post: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `journal`
- `sys.read`
- `notify`
- `peers`
- `location`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | incident log |
| FORTRAN | trebago | spread model |
| Ada | eve | temperature limits |
| APL | kron | sensor grid |
| MUMPS | myra | building tree |
| JOVIAL | - | machine control |
| Scala | - | dataflow |
| Objective-C | - | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id damper
matrix 174        # the graph's slot IS the matrix layer
name Damper
needs journal
needs sys.read
needs notify
needs peers
needs location
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

