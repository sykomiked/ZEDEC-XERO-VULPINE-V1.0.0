# Criteria — theorbo

| | |
|---|---|
| app | `theorbo` |
| matrix layer | 348 |
| icon group | L3 |
| wave spec | `wave29/348-theorbo.md` |

## Purpose

Composes, records and performs music with others: scores and stems in the Hoard (`files.read`, `files.write`), rehearsals in a hall (`hall.load`, `hall.play`, `hall.pause`, `hall.seek`, `hall.status`, `pspace.create`), royalty splits paid only on the owners' approval (`ledger.post`). Audio in and out has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `mic`
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
| COBOL | sutra | royalty splits |
| FORTRAN | trebago | audio synthesis |
| Ada | eve | loudness limits |
| APL | kron | score arrays |
| MUMPS | myra | song version tree |
| PostScript | - | page layout |
| Simula I | - | simulation |
| LabVIEW | - | signals |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id theorbo
matrix 348        # the graph's slot IS the matrix layer
name Theorbo
needs fs.read
needs fs.write
needs mic
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

