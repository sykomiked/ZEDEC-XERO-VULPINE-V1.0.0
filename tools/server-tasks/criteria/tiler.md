# Criteria — tiler

| | |
|---|---|
| app | `tiler` |
| matrix layer | 203 |
| icon group | L2 |
| wave spec | `wave16/203-tiler.md` |

## Purpose

assembles live dashboards from tiles drawn from the machine's data and monitors (`sys.info`, `sys.disk`, `sys.net`, `power.status`, `log.tail`), kept in the Hoard (`files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `sys.read`
- `journal`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | KPI records |
| FORTRAN | trebago | aggregations |
| Ada | eve | refresh limits |
| APL | kron | tile grid |
| MUMPS | myra | dashboard tree |
| SAS | - | reports |
| Logo | logo | geometry |
| Visual Basic | dotnet | view and model |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tiler
matrix 203        # the graph's slot IS the matrix layer
name Tiler
needs sys.read
needs journal
needs fs.read
needs fs.write
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

