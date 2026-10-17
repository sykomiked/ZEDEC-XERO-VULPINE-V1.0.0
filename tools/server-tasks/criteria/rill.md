# Criteria — rill

| | |
|---|---|
| app | `rill` |
| matrix layer | 216 |
| icon group | L2 |
| wave spec | `wave17/216-rill.md` |

## Purpose

gives children a safe reading space with read-aloud, word help and story choices, from the Hoard (`files.read`) within their screen time (`screen.check`, `screen.status`). zxvd has no method for text-to-speech: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `screen.time`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | reading log |
| FORTRAN | trebago | reading-level model |
| Ada | eve | age limits |
| APL | kron | vocabulary grid |
| MUMPS | myra | child profile tree |
| BASIC | basic | step-by-step teaching |
| Mojo | - | AI |
| AWK | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id rill
matrix 216        # the graph's slot IS the matrix layer
name Rill
needs fs.read
needs screen.time
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

