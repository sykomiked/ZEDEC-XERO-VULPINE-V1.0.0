# Criteria — glebe

| | |
|---|---|
| app | `glebe` |
| matrix layer | 255 |
| icon group | L2 |
| wave spec | `wave21/255-glebe.md` |

## Purpose

Plans harvests: fields and the place (`place.here`, `place.list`), crop sales (`net.offers`, `net.command` sell, `ledger.entries`), plans in the Hoard (`files.read`, `files.write`), farm energy (`power.status`). There is no weather source (as Weatherglass): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `peers`
- `net`
- `ledger.read`
- `fs.read`
- `fs.write`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | crop sales |
| FORTRAN | trebago | yield model |
| Ada | eve | storage limits |
| APL | kron | field grid |
| MUMPS | myra | farm and field tree |
| Simula 67 | - | simulation |
| Modula-2 | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id glebe
matrix 255        # the graph's slot IS the matrix layer
name Glebe
needs location
needs peers
needs net
needs ledger.read
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

