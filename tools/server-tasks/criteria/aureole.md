# Criteria — aureole

| | |
|---|---|
| app | `aureole` |
| matrix layer | 277 |
| icon group | L3 |
| wave spec | `wave23/277-aureole.md` |

## Purpose

runs a town or co-op council: agendas, motions, minutes, budgets and signed decisions in one public record (`spaces.propose`, `spaces.vote`, `spaces.get`, `journal.query`, `web4.verify`, `files.write`).

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `journal`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | municipal budget |
| FORTRAN | trebago | revenue projections |
| Ada | eve | quorum rules |
| APL | kron | vote tallies |
| MUMPS | myra | bylaw hierarchy |
| PHP | php | reports |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id aureole
matrix 277        # the graph's slot IS the matrix layer
name Aureole
needs peers
needs net
needs journal
needs fs.write
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

