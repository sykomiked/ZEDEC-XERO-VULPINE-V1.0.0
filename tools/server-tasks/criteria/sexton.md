# Criteria — sexton

| | |
|---|---|
| app | `sexton` |
| matrix layer | 195 |
| icon group | L2 |
| wave spec | `wave16/195-sexton.md` |

## Purpose

responds to outages and incidents: raises alarms (`log.tail`, `journal.query`, `sys.info`), rallies people (`chat.post`, `call.start` on a person's say), and runs recovery (`backup.verify`).

## Capabilities (needs — the exact registry names)

- `journal`
- `sys.read`
- `peers`
- `net`
- `notify`
- `backup`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | incident record |
| FORTRAN | trebago | impact estimate |
| Ada | eve | response deadlines |
| APL | kron | alert matrix |
| MUMPS | myra | on-call tree |
| Elixir | - | fault handling |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id sexton
matrix 195        # the graph's slot IS the matrix layer
name Sexton
needs journal
needs sys.read
needs peers
needs net
needs notify
needs backup
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

