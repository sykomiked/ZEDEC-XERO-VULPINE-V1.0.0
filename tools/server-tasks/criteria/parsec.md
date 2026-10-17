# Criteria — parsec

| | |
|---|---|
| app | `parsec` |
| matrix layer | 311 |
| icon group | L3 |
| wave spec | `wave26/311-parsec.md` |

## Purpose

controls a telescope for deep-sky imaging and shares calibrated images with citizen science projects (`place.here`, `sys.info`, `files.write`). zxvd has no method for mount control, and sharing to citizen science projects (none of the parts holds net): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `sys.read`
- `camera`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | observing time accounts |
| FORTRAN | trebago | image stacking maths |
| Ada | eve | mount slew limits |
| APL | kron | pixel arrays |
| MUMPS | myra | target catalogue |
| MATLAB | - | matrices and images |
| Erlang | - | messaging |
| Modula-2 | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id parsec
matrix 311        # the graph's slot IS the matrix layer
name Parsec
needs location
needs sys.read
needs camera
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

