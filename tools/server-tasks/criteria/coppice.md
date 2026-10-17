# Criteria — coppice

| | |
|---|---|
| app | `coppice` |
| matrix layer | 192 |
| icon group | L2 |
| wave spec | `wave15/192-coppice.md` |

## Purpose

manages woodland: tree inventory, growth, felling plans and carbon held, with places (`place.list`, `place.add`) and records in the Hoard (`files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `fs.write`
- `ledger.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | timber and carbon accounts |
| FORTRAN | trebago | growth model |
| Ada | eve | felling limits |
| APL | kron | stand grid |
| MUMPS | myra | tree inventory |
| Scheme | scheme | symbolic reasoning |
| Simula 67 | - | simulation |
| Modula-2 | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id coppice
matrix 192        # the graph's slot IS the matrix layer
name Coppice
needs location
needs fs.read
needs fs.write
needs ledger.read
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

