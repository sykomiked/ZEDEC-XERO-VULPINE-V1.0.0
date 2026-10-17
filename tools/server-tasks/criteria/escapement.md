# Criteria — escapement

| | |
|---|---|
| app | `escapement` |
| matrix layer | 172 |
| icon group | L2 |
| wave spec | `wave14/172-escapement.md` |

## Purpose

keeps exact time across devices (`sys.info`, `devmesh.devices`, `net.peers`) and runs shifts, timesheets and timed jobs from it. zxvd has no method for a peer clock-offset query: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `peers`
- `notify`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | timesheets |
| FORTRAN | trebago | drift correction |
| Ada | eve | deadline limits |
| APL | kron | schedule matrix |
| MUMPS | myra | shift tree |
| Go | go | concurrency |
| Bourne shell | - | jobs |
| Crystal | - | low-level system |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id escapement
matrix 172        # the graph's slot IS the matrix layer
name Escapement
needs sys.read
needs peers
needs notify
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

