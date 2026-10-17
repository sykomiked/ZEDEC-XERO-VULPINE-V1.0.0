# Criteria — creel

| | |
|---|---|
| app | `creel` |
| matrix layer | 187 |
| icon group | L2 |
| wave spec | `wave15/187-creel.md` |

## Purpose

logs catches, quotas and fishing spots (`place.here`, `place.add`, `place.list`) with weather and sales in the books (`ledger.entries`, `ledger.post`, `ledger.hold`).

## Capabilities (needs — the exact registry names)

- `location`
- `ledger.read`
- `ledger.post`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | catch sales |
| FORTRAN | trebago | stock model |
| Ada | eve | quota limits |
| APL | kron | catch grid |
| MUMPS | myra | vessel tree |
| Icon | - | patterns |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id creel
matrix 187        # the graph's slot IS the matrix layer
name Creel
needs location
needs ledger.read
needs ledger.post
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

