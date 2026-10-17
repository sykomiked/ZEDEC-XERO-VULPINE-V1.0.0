# Criteria — hornbook

| | |
|---|---|
| app | `hornbook` |
| matrix layer | 169 |
| icon group | L2 |
| wave spec | `wave14/169-hornbook.md` |

## Purpose

builds lessons with diagrams, slides and flashcards in the Hoard (`files.read`, `files.write`), then tracks each learner's progress.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | enrolment records |
| FORTRAN | trebago | score statistics |
| Ada | eve | age-appropriate limits |
| APL | kron | mastery grid |
| MUMPS | myra | learner tree |
| BASIC | basic | step-by-step teaching |
| Mojo | - | AI |
| Bash | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id hornbook
matrix 169        # the graph's slot IS the matrix layer
name Hornbook
needs fs.read
needs fs.write
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

