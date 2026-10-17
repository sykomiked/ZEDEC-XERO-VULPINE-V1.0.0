# Criteria — weir

| | |
|---|---|
| app | `weir` |
| matrix layer | 237 |
| icon group | L2 |
| wave spec | `wave19/237-weir.md` |

## Purpose

Maps household water: pipes, valves, flow, budgets and leak alerts, kept in the Hoard (`files.read`, `files.write`), event history (`journal.query`, `log.tail`). Water meters and valve actuators have no method (as Understory and Homestead): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | utility bills |
| FORTRAN | trebago | flow model |
| Ada | eve | pressure limits |
| APL | kron | pipe matrix |
| MUMPS | myra | fixture tree |
| Forth | forth | machine control |
| Gleam | - | messaging |
| Scala | - | dataflow |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id weir
matrix 237        # the graph's slot IS the matrix layer
name Weir
needs fs.read
needs fs.write
needs journal
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

