# Criteria — glazier

| | |
|---|---|
| app | `glazier` |
| matrix layer | 193 |
| icon group | L2 |
| wave spec | `wave16/193-glazier.md` |

## Purpose

publishes budgets and decisions from the books (`ledger.accounts`, `ledger.entries`, `ledger.check`) openly (`aipi.put`, `web4.cid`, `spaces.set_cid`, `files.write`) so anyone can verify them, with the journal checked by `journal.verify`.

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `fs.read`
- `fs.write`
- `net`
- `peers`
- `journal`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | public accounts |
| FORTRAN | trebago | spending trends |
| Ada | eve | disclosure rules |
| APL | kron | budget grid |
| MUMPS | myra | agency tree |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id glazier
matrix 193        # the graph's slot IS the matrix layer
name Glazier
needs ledger.read
needs fs.read
needs fs.write
needs net
needs peers
needs journal
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

