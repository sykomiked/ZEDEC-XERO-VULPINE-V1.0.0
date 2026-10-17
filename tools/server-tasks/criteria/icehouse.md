# Criteria — icehouse

| | |
|---|---|
| app | `icehouse` |
| matrix layer | 194 |
| icon group | L2 |
| wave spec | `wave16/194-icehouse.md` |

## Purpose

freezes snapshots of systems and data into cold archives (`backup.run`, `backup.list`, `backup.verify`) and thaws them on demand (`backup.restore`), within the free space `sys.disk` reports. zxvd has no method for an archive codec (as Hamper reports): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `backup`
- `fs.read`
- `fs.write`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | archive catalogue |
| FORTRAN | trebago | compression ratios |
| Ada | eve | integrity limits |
| APL | kron | block arrays |
| MUMPS | myra | snapshot tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id icehouse
matrix 194        # the graph's slot IS the matrix layer
name Icehouse
needs backup
needs fs.read
needs fs.write
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

