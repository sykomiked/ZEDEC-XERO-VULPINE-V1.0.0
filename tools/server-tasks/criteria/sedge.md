# Criteria — sedge

| | |
|---|---|
| app | `sedge` |
| matrix layer | 248 |
| icon group | L2 |
| wave spec | `wave20/248-sedge.md` |

## Purpose

Tracks household medicines (advice only): doses, schedules, stock and interactions kept in the Hoard (`files.read`, `files.write`, `search.query`), notices (`notes.list`). Posting a reminder has no method (as Bowline): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | stock records |
| FORTRAN | trebago | dose timing |
| Ada | eve | maximum dose limits |
| APL | kron | interaction matrix |
| MUMPS | myra | patient tree |
| Bourne shell | - | jobs |
| PL/I | - | business records |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id sedge
matrix 248        # the graph's slot IS the matrix layer
name Sedge
needs fs.read
needs fs.write
needs search
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

