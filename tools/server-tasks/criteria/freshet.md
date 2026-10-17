# Criteria — freshet

| | |
|---|---|
| app | `freshet` |
| matrix layer | 209 |
| icon group | L2 |
| wave spec | `wave17/209-freshet.md` |

## Purpose

hatches a new app project from a template in the Hoard (`files.mkdir`, `files.write`, `files.read`, `files.list`), with signing, version control and a first build ready. zxvd has no method for build run and signing (as Whetstone and Satchel report): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | licence records |
| FORTRAN | trebago | build timings |
| Ada | eve | declared limits per template |
| APL | kron | template matrix |
| MUMPS | myra | project tree |
| Bourne shell | - | jobs |
| REXX | - | scripting |
| PostScript | - | page layout |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id freshet
matrix 209        # the graph's slot IS the matrix layer
name Freshet
needs fs.read
needs fs.write
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

