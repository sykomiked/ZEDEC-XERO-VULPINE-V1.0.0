# Criteria — flue

| | |
|---|---|
| app | `flue` |
| matrix layer | 189 |
| icon group | L2 |
| wave spec | `wave15/189-flue.md` |

## Purpose

syncs spaces, files and backups across your own devices and your cloud top (`spaces.list`, `files.list`, `files.stat`, `files.cross`, `backup.run`, `backup.list`, `devmesh.devices`, `aipi.put`, `aipi.get`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `backup`
- `peers`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | sync ledger |
| FORTRAN | trebago | bandwidth model |
| Ada | eve | quota limits |
| APL | kron | version vectors |
| MUMPS | myra | folder tree |
| Elixir | - | messaging |
| System/360 assembler | - | low-level system |
| TypeScript | typescript | view and model |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id flue
matrix 189        # the graph's slot IS the matrix layer
name Flue
needs fs.read
needs fs.write
needs backup
needs peers
needs net
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

