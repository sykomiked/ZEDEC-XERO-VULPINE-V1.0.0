# Criteria — lampwright

| | |
|---|---|
| app | `lampwright` |
| matrix layer | 205 |
| icon group | L2 |
| wave spec | `wave17/205-lampwright.md` |

## Purpose

sets ambient lighting and display scenes across rooms and screens with colour care, reading `ui.get`, `sys.info` and `power.status`. zxvd has no method for lighting device control, display colour set: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `net`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | energy use |
| FORTRAN | trebago | colour math |
| Ada | eve | brightness limits |
| APL | kron | light grid |
| MUMPS | myra | room tree |
| Dart | dart | view and model |
| Ladder logic | - | machine control |
| Smalltalk | - | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id lampwright
matrix 205        # the graph's slot IS the matrix layer
name Lampwright
needs sys.read
needs net
needs peers
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

