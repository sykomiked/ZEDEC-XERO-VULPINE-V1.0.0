# Criteria — thaw

| | |
|---|---|
| app | `thaw` |
| matrix layer | 266 |
| icon group | L2 |
| wave spec | `wave22/266-thaw.md` |

## Purpose

Rebuilds a whole machine after disaster from backups, keys and settings (kernel/src/recover): backups (`backup.list`, `backup.verify`, `backup.versions`, `backup.restore`), checks (`journal.verify`, `ledger.check`, `sys.disk`), guardians (`recovery.status`, `recovery.claim`), spaces and files (`spaces.list`, `files.cross`), switches (`features.get`).

## Capabilities (needs — the exact registry names)

- `backup`
- `fs.read`
- `fs.write`
- `journal`
- `ledger.read`
- `sys.read`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | restore records |
| FORTRAN | trebago | restore time estimate |
| Ada | eve | integrity limits |
| APL | kron | block map |
| MUMPS | myra | system tree |
| Erlang | - | fault handling |
| JCL | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id thaw
matrix 266        # the graph's slot IS the matrix layer
name Thaw
needs backup
needs fs.read
needs fs.write
needs journal
needs ledger.read
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

