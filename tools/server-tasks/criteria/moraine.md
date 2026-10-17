# Criteria — moraine

| | |
|---|---|
| app | `moraine` |
| matrix layer | 210 |
| icon group | L2 |
| wave spec | `wave17/210-moraine.md` |

## Purpose

holds keys in escrow with guardians (`recovery.setup`, `recovery.status`, `recovery.approve`, `recovery.veto`, `recovery.claim`, `recovery.finalize`, `recovery.cancel`), grants time-limited access, and logs every release (`journal.query`).

## Capabilities (needs — the exact registry names)

- `peers`
- `journal`
- `notify`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | custody register |
| FORTRAN | trebago | key-strength scoring |
| Ada | eve | release limits |
| APL | kron | access grid |
| MUMPS | myra | guardian hierarchy |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id moraine
matrix 210        # the graph's slot IS the matrix layer
name Moraine
needs peers
needs journal
needs notify
needs sys.read
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

