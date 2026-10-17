# Criteria — vernier

| | |
|---|---|
| app | `vernier` |
| matrix layer | 341 |
| icon group | L3 |
| wave spec | `wave28/341-vernier.md` |

## Purpose

inspects and grades finished parts or stones to fine tolerance, issuing a signed certificate for each piece (`files.read`, `files.write`, `journal.query`); the certificate is a Hoard file.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `camera`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | grading certificates |
| FORTRAN | trebago | tolerance maths |
| Ada | eve | cut limits |
| APL | kron | facet angle arrays |
| MUMPS | myra | provenance tree |
| RPG | - | reports |
| ML | - | proofs of correctness |
| ALGOL 60 | algol60 | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id vernier
matrix 341        # the graph's slot IS the matrix layer
name Vernier
needs fs.read
needs fs.write
needs journal
needs camera
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

