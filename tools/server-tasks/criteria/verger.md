# Criteria — verger

| | |
|---|---|
| app | `verger` |
| matrix layer | 206 |
| icon group | L2 |
| wave spec | `wave17/206-verger.md` |

## Purpose

organises a congregation or club: members and groups (`social.groups`, `social.group_create`), gatherings, giving in the books (`ledger.entries`, `ledger.balance`, `ledger.post`), and calendars in the Hoard (`files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | giving ledger |
| FORTRAN | trebago | attendance trends |
| Ada | eve | consent limits |
| APL | kron | rota grid |
| MUMPS | myra | member tree |
| Python | python | AI |
| Pascal | pascal | step-by-step teaching |
| Swift | swift | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id verger
matrix 206        # the graph's slot IS the matrix layer
name Verger
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
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

