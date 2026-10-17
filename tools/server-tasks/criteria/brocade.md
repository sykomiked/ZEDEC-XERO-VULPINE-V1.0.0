# Criteria — brocade

| | |
|---|---|
| app | `brocade` |
| matrix layer | 376 |
| icon group | L3 |
| wave spec | `wave31/376-brocade.md` |

## Purpose

Designs, shows and sells fashion collections: patterns and lookbooks in the Hoard (`files.read`, `files.write`), shows as a hall (`pspace.create`, `hall.load`, `hall.play`, `hall.status`), made-to-order sales in the books (`ledger.post`, `ledger.entries`), cut sheets printed (`print.submit`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `print`
- `peers`
- `net`
- `ledger.read`
- `ledger.post`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | order ledger |
| FORTRAN | trebago | fabric yield |
| Ada | eve | size limits |
| APL | kron | colourway grid |
| MUMPS | myra | collection tree |
| TRAC | - | text |
| Icon | - | patterns |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id brocade
matrix 376        # the graph's slot IS the matrix layer
name Brocade
needs fs.read
needs fs.write
needs print
needs peers
needs net
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

