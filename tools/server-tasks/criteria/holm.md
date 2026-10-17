# Criteria — holm

| | |
|---|---|
| app | `holm` |
| matrix layer | 267 |
| icon group | L2 |
| wave spec | `wave22/267-holm.md` |

## Purpose

Locks a device into a hardened offline mode for kiosks or sensitive work: network off (`net.command` off), answering peers stopped (`peer.serve`), the phone link (`phone.link`), switches (`features.get`, `features.set`), simple mode (`ui.get`, `ui.set`), seats (`seats.lock`, `seats.set_auto`).

## Capabilities (needs — the exact registry names)

- `net`
- `peers`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | change records |
| FORTRAN | trebago | risk scoring |
| Ada | eve | hard limits |
| APL | kron | policy matrix |
| MUMPS | myra | config tree |
| BCPL | - | low-level system |
| HyperTalk | - | view and model |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id holm
matrix 267        # the graph's slot IS the matrix layer
name Holm
needs net
needs peers
needs sys.read
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

