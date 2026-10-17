# Criteria — sundial

| | |
|---|---|
| app | `sundial` |
| matrix layer | 110 |
| icon group | L1 |
| wave spec | `wave9/110-sundial.md` |

## Purpose

clock, alarms, timers, world time. Read time from `sys.info`. zxvd has no alarm, timer, time-zone or notice-post method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| FORTRAN | trebago | exact numerics |
| APL | kron | arrays |
| Rust | rust | safety rules |
| Ada | eve | safety rules |
| Bash | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id sundial
matrix 110        # the graph's slot IS the matrix layer
name Sundial
needs sys.read
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

