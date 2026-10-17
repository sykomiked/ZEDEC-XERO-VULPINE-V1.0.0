# Criteria — descant

| | |
|---|---|
| app | `descant` |
| matrix layer | 397 |
| icon group | L3 |
| wave spec | `wave33/397-descant.md` |

## Purpose

Grows a personal learning tree: goals and lessons in the Hoard (`files.read`, `files.write`, `search.query`), lessons run (`play.catalogue`, `play.run`), credentials signed by a teacher and checked (`web4.verify`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `ai.ask`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | credential records |
| FORTRAN | trebago | mastery estimates |
| Ada | eve | pace limits |
| APL | kron | skill grid |
| MUMPS | myra | learning tree |
| BASIC | basic | step-by-step teaching |
| ML | - | proofs of correctness |
| ALGOL 60 | algol60 | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id descant
matrix 397        # the graph's slot IS the matrix layer
name Descant
needs fs.read
needs fs.write
needs search
needs ai.ask
needs net
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

