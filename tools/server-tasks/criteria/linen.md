# Criteria — linen

| | |
|---|---|
| app | `linen` |
| matrix layer | 363 |
| icon group | L3 |
| wave spec | `wave30/363-linen.md` |

## Purpose

Tracks crops, soil, carbon and biodiversity across a farm or forest, and earns credits for verified care: fields (`place.list`, `place.add`), records (`files.read`, `files.write`), the books (`ledger.post`, `ledger.hold`, `ledger.holds`, `ledger.release`). Satellite imagery and soil sensors have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
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
| COBOL | sutra | carbon credits |
| FORTRAN | trebago | growth models |
| Ada | eve | chemical limits |
| APL | kron | field grid |
| MUMPS | myra | plot tree |
| ML | - | proofs of correctness |
| ALGOL 60 | algol60 | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id linen
matrix 363        # the graph's slot IS the matrix layer
name Linen
needs location
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

