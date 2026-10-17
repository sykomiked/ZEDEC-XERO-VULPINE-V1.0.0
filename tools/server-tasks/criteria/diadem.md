# Criteria — diadem

| | |
|---|---|
| app | `diadem` |
| matrix layer | 321 |
| icon group | L3 |
| wave spec | `wave27/321-diadem.md` |

## Purpose

runs elections and votes for clubs, co-ops and towns: ballots, eligibility, counting and verifiable results (`spaces.propose`, `spaces.vote`, `spaces.get`, `seats.list`, `journal.query`, `journal.verify`).

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `journal`
- `fs.write`
- `notify`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | election costs |
| FORTRAN | trebago | tally statistics |
| Ada | eve | eligibility rules |
| APL | kron | ballot arrays |
| MUMPS | myra | voter roll tree |
| Prolog | - | rules |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id diadem
matrix 321        # the graph's slot IS the matrix layer
name Diadem
needs peers
needs net
needs journal
needs fs.write
needs notify
needs sys.read
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

