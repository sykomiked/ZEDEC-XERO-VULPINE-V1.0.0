# Criteria — quay

| | |
|---|---|
| app | `quay` |
| matrix layer | 316 |
| icon group | L3 |
| wave spec | `wave26/316-quay.md` |

## Purpose

runs a port or marina: berths, arrivals, cargo handling, customs papers and fees (`place.list`, `files.read`, `files.write`, `ledger.entries`, `ledger.post`, `print.submit`).

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `print`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | harbour dues |
| FORTRAN | trebago | berth scheduling |
| Ada | eve | draught and load limits |
| APL | kron | berth grid |
| MUMPS | myra | vessel and cargo tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id quay
matrix 316        # the graph's slot IS the matrix layer
name Quay
needs location
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs print
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

