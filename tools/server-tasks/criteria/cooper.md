# Criteria — cooper

| | |
|---|---|
| app | `cooper` |
| matrix layer | 208 |
| icon group | L2 |
| wave spec | `wave17/208-cooper.md` |

## Purpose

tracks goods through reuse, repair and recycling loops in the cooperative market (`net.offers`, `ledger.entries`), records in the Hoard (`files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ledger.read`
- `net`
- `peers`
- `camera`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | material ledger |
| FORTRAN | trebago | lifecycle model |
| Ada | eve | waste limits |
| APL | kron | flow grid |
| MUMPS | myra | product tree |
| Erlang | - | fault handling |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cooper
matrix 208        # the graph's slot IS the matrix layer
name Cooper
needs fs.read
needs fs.write
needs ledger.read
needs net
needs peers
needs camera
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

