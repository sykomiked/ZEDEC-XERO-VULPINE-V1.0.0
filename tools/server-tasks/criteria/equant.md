# Criteria — equant

| | |
|---|---|
| app | `equant` |
| matrix layer | 320 |
| icon group | L3 |
| wave spec | `wave27/320-equant.md` |

## Purpose

arbitrates disputes: claims, evidence, hearings, rulings and enforcement under a published rulebook (`files.read`, `files.write`, `journal.query`, `web4.verify`, `pspace.post`, `ledger.hold`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `peers`
- `net`
- `ledger.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | settlement amounts |
| FORTRAN | trebago | damages calculation |
| Ada | eve | procedure deadlines |
| APL | kron | evidence matrix |
| MUMPS | myra | case hierarchy |
| Prolog | - | rules |
| TeX | - | typesetting |
| SNOBOL | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id equant
matrix 320        # the graph's slot IS the matrix layer
name Equant
needs fs.read
needs fs.write
needs journal
needs peers
needs net
needs ledger.read
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

