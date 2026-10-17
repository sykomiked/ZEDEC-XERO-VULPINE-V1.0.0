# Criteria — heartstring

| | |
|---|---|
| app | `heartstring` |
| matrix layer | 357 |
| icon group | L3 |
| wave spec | `wave30/357-heartstring.md` |

## Purpose

Looks after someone you love: shared care plans, medication reminders, mood check-ins and family updates in the Hoard (`files.read`, `files.write`), the family space (`spaces.get`, `spaces.list`), notices (`notes.list`, `notes.mark_read`), alarms from the home (`log.tail`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `peers`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | care costs |
| FORTRAN | trebago | trend smoothing |
| Ada | eve | dose and alert limits |
| APL | kron | mood grid |
| MUMPS | myra | family care tree |
| Go | go | messaging |
| IPL | - | lists |
| Simula 67 | - | simulation |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id heartstring
matrix 357        # the graph's slot IS the matrix layer
name Heartstring
needs fs.read
needs fs.write
needs peers
needs journal
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

