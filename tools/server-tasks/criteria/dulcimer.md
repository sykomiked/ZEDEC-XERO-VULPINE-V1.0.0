# Criteria — dulcimer

| | |
|---|---|
| app | `dulcimer` |
| matrix layer | 342 |
| icon group | L3 |
| wave spec | `wave28/342-dulcimer.md` |

## Purpose

designs pieces, tracks metal and stones, schedules bench work and sells finished goods (`files.read`, `files.write`, `ledger.entries`, `ledger.post`, `net.offers`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `camera`
- `net`
- `peers`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | workshop orders |
| FORTRAN | trebago | alloy mix |
| Ada | eve | furnace temperature limits |
| APL | kron | stock grid |
| MUMPS | myra | piece lineage tree |
| Bourne shell | - | jobs |
| Kotlin | kotlin | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id dulcimer
matrix 342        # the graph's slot IS the matrix layer
name Dulcimer
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs camera
needs net
needs peers
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

