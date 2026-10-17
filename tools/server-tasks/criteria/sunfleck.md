# Criteria — sunfleck

| | |
|---|---|
| app | `sunfleck` |
| matrix layer | 330 |
| icon group | L3 |
| wave spec | `wave27/330-sunfleck.md` |

## Purpose

plans and verifies tree planting and carbon storage, with growth records that buyers of credits can audit (`place.list`, `files.read`, `files.write`, `ledger.entries`, `ledger.post`, `ledger.hold`).

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | credit sales |
| FORTRAN | trebago | biomass growth model |
| Ada | eve | credit issuance limits |
| APL | kron | plot grid |
| MUMPS | myra | stand and tree hierarchy |
| Simula 67 | - | simulation |
| Oberon | - | structure |
| SAS | - | reports |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id sunfleck
matrix 330        # the graph's slot IS the matrix layer
name Sunfleck
needs location
needs fs.read
needs fs.write
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

