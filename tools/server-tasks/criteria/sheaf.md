# Criteria — sheaf

| | |
|---|---|
| app | `sheaf` |
| matrix layer | 170 |
| icon group | L2 |
| wave spec | `wave14/170-sheaf.md` |

## Purpose

keeps the farm ledger: fields, harvests, livestock, sales and weather in one place, in the books (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.hold`) and the Hoard (`files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | farm accounts |
| FORTRAN | trebago | yield model |
| Ada | eve | chemical limits |
| APL | kron | field grid |
| MUMPS | myra | herd tree |
| Forth | forth | small interpreters |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id sheaf
matrix 170        # the graph's slot IS the matrix layer
name Sheaf
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
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

