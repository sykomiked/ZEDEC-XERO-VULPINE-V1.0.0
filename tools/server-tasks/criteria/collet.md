# Criteria — collet

| | |
|---|---|
| app | `collet` |
| matrix layer | 309 |
| icon group | L3 |
| wave spec | `wave26/309-collet.md` |

## Purpose

locks sensitive data in escrow that opens only under agreed rules, such as two keyholders or a set date (`recovery.setup`, `recovery.status`, `recovery.approve`, `recovery.veto`, `files.cross`, `journal.query`).

## Capabilities (needs — the exact registry names)

- `peers`
- `journal`
- `notify`
- `fs.read`
- `fs.write`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | escrow fees |
| FORTRAN | trebago | key splitting maths |
| Ada | eve | release conditions |
| APL | kron | keyholder matrix |
| MUMPS | myra | release policy tree |
| Prolog | - | rules |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id collet
matrix 309        # the graph's slot IS the matrix layer
name Collet
needs peers
needs journal
needs notify
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

