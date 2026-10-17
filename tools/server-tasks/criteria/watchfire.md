# Criteria — watchfire

| | |
|---|---|
| app | `watchfire` |
| matrix layer | 143 |
| icon group | L1 |
| wave spec | `wave11/143-watchfire.md` |

## Purpose

emergency contacts and a medical ID shown on the lock screen; on the person's request, ring contacts (`call.start`, signalling only) and share where the machine is (`place.ask`, `place.here`). Contacts' devices from `phone.list`.

## Capabilities (needs — the exact registry names)

- `location`
- `peers`
- `notify`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | safety rules |
| MUMPS | myra | records |
| OCaml | - | parsing |
| Swift | swift | domain objects |
| Python | python | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id watchfire
matrix 143        # the graph's slot IS the matrix layer
name Watchfire
needs location
needs peers
needs notify
needs fs.read
needs fs.write
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

