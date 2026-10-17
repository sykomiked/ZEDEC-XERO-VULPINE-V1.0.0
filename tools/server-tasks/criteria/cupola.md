# Criteria — cupola

| | |
|---|---|
| app | `cupola` |
| matrix layer | 282 |
| icon group | L3 |
| wave spec | `wave23/282-cupola.md` |

## Purpose

preserves records for decades: checks integrity, migrates old formats and keeps copies on disk, on peers and as content-addressed blocks (`backup.verify`, `backup.versions`, `aipi.put`, `aipi.get`, `web4.cid`, `files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `backup`
- `fs.read`
- `fs.write`
- `net`
- `search`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | retention ledger |
| FORTRAN | trebago | decay risk model |
| Ada | eve | retention limits |
| APL | kron | copy integrity grid |
| MUMPS | myra | archive hierarchy |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |
| Go | go | messaging |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cupola
matrix 282        # the graph's slot IS the matrix layer
name Cupola
needs backup
needs fs.read
needs fs.write
needs net
needs search
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

