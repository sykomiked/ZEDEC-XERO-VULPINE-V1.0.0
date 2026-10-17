# Criteria — stereoscope

| | |
|---|---|
| app | `stereoscope` |
| matrix layer | 350 |
| icon group | L3 |
| wave spec | `wave29/350-stereoscope.md` |

## Purpose

Shoots, edits and publishes photo stories with signed originals, tracked edits and licences sold per image: photos in the Hoard (`files.read`, `files.write`, `sys.disk`), published by CID (`aipi.put`, `web4.cid`), signatures checked (`web4.verify`), licence sales in the books (`ledger.post`, `ledger.entries`).

## Capabilities (needs — the exact registry names)

- `camera`
- `fs.read`
- `fs.write`
- `sys.read`
- `net`
- `ledger.read`
- `ledger.post`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | licence sales |
| FORTRAN | trebago | colour science |
| Ada | eve | exposure limits |
| APL | kron | pixel arrays |
| MUMPS | myra | shoot album tree |
| MATLAB | - | matrices and images |
| ML | - | proofs of correctness |
| ALGOL 60 | algol60 | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id stereoscope
matrix 350        # the graph's slot IS the matrix layer
name Stereoscope
needs camera
needs fs.read
needs fs.write
needs sys.read
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

