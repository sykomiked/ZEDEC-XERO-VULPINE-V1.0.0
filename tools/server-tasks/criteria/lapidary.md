# Criteria — lapidary

| | |
|---|---|
| app | `lapidary` |
| matrix layer | 191 |
| icon group | L2 |
| wave spec | `wave15/191-lapidary.md` |

## Purpose

designs schematics, boards and chip logic in the Hoard (`files.read`, `files.write`), simulates them, and publishes defensive-publication packets under the ZXV Hardware License (`aipi.put`, `web4.cid`), every revision on the journal (`journal.query`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | design revision ledger |
| FORTRAN | trebago | circuit maths |
| Ada | eve | electrical safety limits |
| APL | kron | netlist arrays |
| MUMPS | myra | part library |
| VHDL | - | circuits |
| Zig | - | low-level system |
| Mathematica | - | symbolic reasoning |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id lapidary
matrix 191        # the graph's slot IS the matrix layer
name Lapidary
needs fs.read
needs fs.write
needs journal
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

