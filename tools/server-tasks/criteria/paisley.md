# Criteria — paisley

| | |
|---|---|
| app | `paisley` |
| matrix layer | 373 |
| icon group | L3 |
| wave spec | `wave31/373-paisley.md` |

## Purpose

Designs symmetric patterns, textiles, tiles and logos with live radial symmetry, then exports to print or loom: designs in the Hoard (`files.read`, `files.write`), printed (`print.status`, `print.submit`, `print.job`), print orders in the books (`ledger.post`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `print`
- `ledger.read`
- `ledger.post`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | print orders |
| FORTRAN | trebago | symmetry maths |
| Ada | eve | printer limits |
| APL | kron | pattern arrays |
| MUMPS | myra | design version tree |
| APT | - | tool paths |
| Logo | logo | geometry |
| SNOBOL | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id paisley
matrix 373        # the graph's slot IS the matrix layer
name Paisley
needs fs.read
needs fs.write
needs print
needs ledger.read
needs ledger.post
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

