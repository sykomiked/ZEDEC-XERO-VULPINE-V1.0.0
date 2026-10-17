# Criteria — cove

| | |
|---|---|
| app | `cove` |
| matrix layer | 32 |
| icon group | L1 |
| wave spec | `wave5/032-cove.md` |

## Purpose

only between devices on the owner's roster (`devmesh.devices`, `devmesh.status`); frames sealed and opened with `ehop.seal`, `ehop.open` on a band from `ehop.bands`. No carrier is wired to the socket (H7): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| Go | go | messaging |
| System/360 assembler | - | low-level system |
| Objective-C | - | view and model |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cove
matrix 32        # the graph's slot IS the matrix layer
name Cove
needs peers
needs net
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

