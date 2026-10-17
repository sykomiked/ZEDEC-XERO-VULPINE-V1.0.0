# Criteria — temper

| | |
|---|---|
| app | `temper` |
| matrix layer | 66 |
| icon group | L1 |
| wave spec | `wave7/066-temper.md` |

## Purpose

the companion's feeling (`companion.mood`), the swarm's budgets and cycles (`swarm.state`), its earnings in the books (`swarm.earnings`), energy budgets (`power.budget`).

## Capabilities (needs — the exact registry names)

- `swarm`
- `ledger.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| Ada | eve | limits |
| Mojo | - | AI |
| COBOL | sutra | money and ledgers |
| C++ | cpp | money and ledgers |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id temper
matrix 66        # the graph's slot IS the matrix layer
name Temper
needs swarm
needs ledger.read
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

