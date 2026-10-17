# Criteria — assize

| | |
|---|---|
| app | `assize` |
| matrix layer | 178 |
| icon group | L2 |
| wave spec | `wave14/178-assize.md` |

## Purpose

runs dispute cases: filings, evidence, hearings and rulings in the Hoard (`files.read`, `files.write`), all on the signed record (`journal.query`, `journal.verify`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | case docket |
| FORTRAN | trebago | deadline counts |
| Ada | eve | procedural limits |
| APL | kron | evidence matrix |
| MUMPS | myra | case tree |
| Prolog | - | rules |
| ML | - | proofs of correctness |
| ALGOL 60 | algol60 | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id assize
matrix 178        # the graph's slot IS the matrix layer
name Assize
needs fs.read
needs fs.write
needs journal
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

