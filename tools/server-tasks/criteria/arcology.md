# Criteria — arcology

| | |
|---|---|
| app | `arcology` |
| matrix layer | 325 |
| icon group | L3 |
| wave spec | `wave27/325-arcology.md` |

## Purpose

city services in one place: permits, repairs, transport, utilities and resident requests with public progress (`files.read`, `files.write`, `pspace.post`, `ledger.entries`, `place.list`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `peers`
- `net`
- `ledger.read`
- `location`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | city revenue |
| FORTRAN | trebago | traffic and utility models |
| Ada | eve | service level limits |
| APL | kron | district grid |
| MUMPS | myra | ward and street tree |
| Elixir | - | fault handling |
| JCL | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id arcology
matrix 325        # the graph's slot IS the matrix layer
name Arcology
needs fs.read
needs fs.write
needs peers
needs net
needs ledger.read
needs location
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

