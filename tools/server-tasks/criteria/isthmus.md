# Criteria — isthmus

| | |
|---|---|
| app | `isthmus` |
| matrix layer | 240 |
| icon group | L2 |
| wave spec | `wave19/240-isthmus.md` |

## Purpose

Mediates disputes: the case room (`pspace.create`, `pspace.post`, `chat.post`, `chat.thread`), the record (`journal.query`, `files.read`, `files.write`), signed copies checked (`web4.verify`), printed agreements (`print.submit`).

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `journal`
- `fs.read`
- `fs.write`
- `print`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | case records |
| FORTRAN | trebago | settlement options |
| Ada | eve | procedure limits |
| APL | kron | claim matrix |
| MUMPS | myra | party tree |
| Prolog | - | rules |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id isthmus
matrix 240        # the graph's slot IS the matrix layer
name Isthmus
needs peers
needs net
needs journal
needs fs.read
needs fs.write
needs print
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

