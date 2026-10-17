# Criteria — bitting

| | |
|---|---|
| app | `bitting` |
| matrix layer | 307 |
| icon group | L3 |
| wave spec | `wave25/307-bitting.md` |

## Purpose

issues and revokes digital keys for doors, rooms, vehicles and shared equipment, with a full access history (`passkeys.list`, `passkeys.revoke`, `devmesh.devices`, `devmesh.revoke`, `journal.query`). zxvd has no method for door and vehicle lock control: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `peers`
- `journal`
- `notify`
- `ledger.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | key deposits |
| FORTRAN | trebago | access pattern scoring |
| Ada | eve | time-window limits |
| APL | kron | door permission grid |
| MUMPS | myra | holder tree |
| Ladder logic | - | machine control |
| Go | go | messaging |
| TRAC | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id bitting
matrix 307        # the graph's slot IS the matrix layer
name Bitting
needs sys.read
needs peers
needs journal
needs notify
needs ledger.read
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

