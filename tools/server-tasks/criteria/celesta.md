# Criteria — celesta

| | |
|---|---|
| app | `celesta` |
| matrix layer | 406 |
| icon group | L3 |
| wave spec | `wave34/406-celesta.md` |

## Purpose

Plans mountain trips and winter sports with snow reports, avalanche risk, lift passes and group check-ins: places (`place.here`, `place.list`), check-ins (`chat.post`, `pspace.post`), plans in the Hoard (`files.read`, `files.write`), lift pass payments (`ledger.post`). Snow and avalanche feeds have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `peers`
- `net`
- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | lift pass sales |
| FORTRAN | trebago | snowpack model |
| Ada | eve | slope angle limits |
| APL | kron | terrain grid |
| MUMPS | myra | party tree |
| PHP | php | reports |
| Simula 67 | - | simulation |
| Modula-2 | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id celesta
matrix 406        # the graph's slot IS the matrix layer
name Celesta
needs location
needs peers
needs net
needs fs.read
needs fs.write
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

