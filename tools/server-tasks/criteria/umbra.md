# Criteria — umbra

| | |
|---|---|
| app | `umbra` |
| matrix layer | 314 |
| icon group | L3 |
| wave spec | `wave26/314-umbra.md` |

## Purpose

lets you act under separate personas and pseudonyms online while keeping each unlinkable and accountable (`seats.list`, `web4.agent_card`, `web4.card_verify`, `chat.me`).

## Capabilities (needs — the exact registry names)

- `sys.read`
- `net`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | persona accounts |
| FORTRAN | trebago | linkability scoring |
| Ada | eve | disclosure limits |
| APL | kron | persona permission grid |
| MUMPS | myra | identity tree |
| Smalltalk | - | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id umbra
matrix 314        # the graph's slot IS the matrix layer
name Umbra
needs sys.read
needs net
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

