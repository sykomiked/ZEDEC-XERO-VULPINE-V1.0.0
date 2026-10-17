# Criteria — silt

| | |
|---|---|
| app | `silt` |
| matrix layer | 242 |
| icon group | L2 |
| wave spec | `wave20/242-silt.md` |

## Purpose

Receives, stamps, files and retains official records with a signed trail: records in the Hoard (`files.list`, `files.stat`, `files.read`, `files.write`, `files.mkdir`; disposal only to the Trash with `files.trash` after approval), versions (`backup.versions`), the trail (`journal.query`, `journal.verify`), signed copies checked (`web4.verify`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `backup`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | register of records |
| FORTRAN | trebago | backlog forecasting |
| Ada | eve | retention limits |
| APL | kron | filing grid |
| MUMPS | myra | case file tree |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id silt
matrix 242        # the graph's slot IS the matrix layer
name Silt
needs fs.read
needs fs.write
needs journal
needs backup
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

