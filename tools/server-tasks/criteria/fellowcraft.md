# Criteria — fellowcraft

| | |
|---|---|
| app | `fellowcraft` |
| matrix layer | 207 |
| icon group | L2 |
| wave spec | `wave17/207-fellowcraft.md` |

## Purpose

runs any job twice, in two languages (`play.run`) or on two machines (`peer.ask`, `aipi.execute`), and compares the results, each on the journal (`journal.query`).

## Capabilities (needs — the exact registry names)

- `ai.ask`
- `peers`
- `net`
- `journal`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | comparison ledger |
| FORTRAN | trebago | numeric diff |
| Ada | eve | contract checks |
| APL | kron | four-valued truth table |
| MUMPS | myra | run tree |
| Bash | - | jobs |
| Racket | - | parsing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id fellowcraft
matrix 207        # the graph's slot IS the matrix layer
name Fellowcraft
needs ai.ask
needs peers
needs net
needs journal
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

