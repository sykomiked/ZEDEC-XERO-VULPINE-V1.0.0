# Criteria — glen

| | |
|---|---|
| app | `glen` |
| matrix layer | 220 |
| icon group | L2 |
| wave spec | `wave18/220-glen.md` |

## Purpose

Runs a family hub: shared calendar and contacts in the Hoard (`files.read`, `files.write`), the family's space and members (`spaces.list`, `spaces.get`), seats (`seats.list`), screen-time rules (`screen.status`, `screen.check`, `screen.set_limit` only after the guardians agree), recovery guardians (`recovery.status`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `screen.time`
- `sys.read`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | household records |
| FORTRAN | trebago | schedule load |
| Ada | eve | child limits |
| APL | kron | availability grid |
| MUMPS | myra | family tree |
| Erlang | - | messaging |
| Prolog | - | rules |
| Java | java | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id glen
matrix 220        # the graph's slot IS the matrix layer
name Glen
needs fs.read
needs fs.write
needs screen.time
needs sys.read
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

