# Criteria — tarn

| | |
|---|---|
| app | `tarn` |
| matrix layer | 224 |
| icon group | L2 |
| wave spec | `wave18/224-tarn.md` |

## Purpose

Guides breathing and focus sessions and logs calm and sleep over time (advice only): sessions and logs in the Hoard (`files.read`, `files.write`), quiet windows and notices (`notes.list`, `screen.status`), the clock from `sys.info`. Readings from a heart-rate or sleep sensor have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `screen.time`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | session log |
| FORTRAN | trebago | breath rhythm model |
| Ada | eve | session limits |
| APL | kron | mood grid |
| MUMPS | myra | wellbeing record tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tarn
matrix 224        # the graph's slot IS the matrix layer
name Tarn
needs fs.read
needs fs.write
needs sys.read
needs screen.time
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

