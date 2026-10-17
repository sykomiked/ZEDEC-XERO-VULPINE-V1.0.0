# Criteria — lifeboat

| | |
|---|---|
| app | `lifeboat` |
| matrix layer | 11 |
| icon group | L1 |
| wave spec | `wave4/011-lifeboat.md` |

## Purpose

start safe (every switch off: `features.get`, `features.set`), check (`journal.verify`, `ledger.check`, `sys.disk`, `backup.verify`), restore from Keepsake (`backup.list`, `backup.restore`), check for a signed reinstall (`update.check` only). Bare-metal reinstall and boot-time repair of the Hoard have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `fs.write`
- `backup`
- `journal`
- `ledger.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | safety rules |
| MUMPS | myra | records |
| Elixir | - | fault handling |
| Crystal | - | low-level system |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id lifeboat
matrix 11        # the graph's slot IS the matrix layer
name Lifeboat
needs sys.read
needs fs.read
needs fs.write
needs backup
needs journal
needs ledger.read
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

