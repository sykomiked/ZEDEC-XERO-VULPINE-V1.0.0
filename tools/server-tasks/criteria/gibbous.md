# Criteria — gibbous

| | |
|---|---|
| app | `gibbous` |
| matrix layer | 283 |
| icon group | L3 |
| wave spec | `wave23/283-gibbous.md` |

## Purpose

tracks sleep, light exposure and body clock, suggesting gentle routines for better rest (advice only), from the Hoard (`files.read`, `files.write`), `screen.status` and `sys.info`. zxvd has no method for wearable and light sensor readings: use only existing methods; list what's missing in your report.

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
| COBOL | sutra | wellness plan costs |
| FORTRAN | trebago | circadian model |
| Ada | eve | screen and alarm limits |
| APL | kron | night sensor arrays |
| MUMPS | myra | sleep history |
| Mojo | - | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id gibbous
matrix 283        # the graph's slot IS the matrix layer
name Gibbous
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

