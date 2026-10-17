# Criteria — tintype

| | |
|---|---|
| app | `tintype` |
| matrix layer | 167 |
| icon group | L2 |
| wave spec | `wave13/167-tintype.md` |

## Purpose

captures, develops and catalogues photos and screen grabs in one workflow in the Hoard (`files.list`, `files.stat`, `files.write`), within the free space `sys.disk` reports. zxvd has no method for image decode (as Gridmark reports), display capture (as Glimpse reports): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `camera`
- `fs.read`
- `fs.write`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | rights register |
| FORTRAN | trebago | exposure math |
| Ada | eve | storage limits |
| APL | kron | image arrays |
| MUMPS | myra | album tree |
| Julia | - | matrices and images |
| Clojure | - | dataflow |
| JavaScript | typescript | view and model |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tintype
matrix 167        # the graph's slot IS the matrix layer
name Tintype
needs camera
needs fs.read
needs fs.write
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

