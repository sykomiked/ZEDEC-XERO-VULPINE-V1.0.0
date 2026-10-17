# Criteria — barbican

| | |
|---|---|
| app | `barbican` |
| matrix layer | 272 |
| icon group | L3 |
| wave spec | `wave23/272-barbican.md` |

## Purpose

guards one person's devices, logins and identity keys (`devmesh.devices`, `passkeys.list`, `seats.list`), showing every intrusion attempt from the journal (`journal.query`, `log.tail`) and blocking it in plain words.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `peers`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | breach cost log |
| FORTRAN | trebago | anomaly scores |
| Ada | eve | lockout limits |
| APL | kron | device threat grid |
| MUMPS | myra | identity tree |
| Nim | - | low-level system |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id barbican
matrix 272        # the graph's slot IS the matrix layer
name Barbican
needs sys.read
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

