# Criteria — trestle

| | |
|---|---|
| app | `trestle` |
| matrix layer | 131 |
| icon group | L1 |
| wave spec | `wave11/131-trestle.md` |

## Purpose

lay out native screens and save them as screen files in the Hoard (`files.read`, `files.write`); simple mode from `ui.get`. zxvd has no toolkit layout method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | money and ledgers |
| FORTRAN | trebago | exact numerics |
| Ada | eve | limits |
| APL | kron | arrays |
| MUMPS | myra | records |
| Delphi | - | view and model |
| Racket | - | parsing |
| Scala | - | typed data |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id trestle
matrix 131        # the graph's slot IS the matrix layer
name Trestle
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

