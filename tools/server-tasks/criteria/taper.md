# Criteria — taper

| | |
|---|---|
| app | `taper` |
| matrix layer | 198 |
| icon group | L2 |
| wave spec | `wave16/198-taper.md` |

## Purpose

guards rest: quiet hours (`screen.status`, `screen.check`, `notes.list`), sleep tracking and gentle night reading from the Hoard (`files.read`), time from `sys.info`.

## Capabilities (needs — the exact registry names)

- `notify`
- `screen.time`
- `fs.read`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | sleep records |
| FORTRAN | trebago | rhythm analysis |
| Ada | eve | wake limits |
| APL | kron | night grid |
| MUMPS | myra | household tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id taper
matrix 198        # the graph's slot IS the matrix layer
name Taper
needs notify
needs screen.time
needs fs.read
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

