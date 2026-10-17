# Criteria — reedbed

| | |
|---|---|
| app | `reedbed` |
| matrix layer | 257 |
| icon group | L2 |
| wave spec | `wave21/257-reedbed.md` |

## Purpose

Monitors wetlands and water quality with low-power sensor nodes: sites (`place.list`), records in the Hoard (`files.read`, `files.write`, `search.query`), the dataset published locally (`aipi.put`, `web4.cid`), the link (`sys.net`). Sensor input has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `net`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | permit records |
| FORTRAN | trebago | water chemistry model |
| Ada | eve | alert thresholds |
| APL | kron | sensor grid |
| MUMPS | myra | site tree |
| JOVIAL | - | machine control |
| Scala | - | dataflow |
| Forth | forth | small interpreters |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id reedbed
matrix 257        # the graph's slot IS the matrix layer
name Reedbed
needs fs.read
needs fs.write
needs search
needs net
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

