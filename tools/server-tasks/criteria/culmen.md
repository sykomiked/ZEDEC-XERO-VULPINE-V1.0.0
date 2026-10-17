# Criteria — culmen

| | |
|---|---|
| app | `culmen` |
| matrix layer | 305 |
| icon group | L3 |
| wave spec | `wave25/305-culmen.md` |

## Purpose

one dashboard across an organisation: money, people, risks, projects and AI agents (`ledger.accounts`, `ledger.balance`, `swarm.state`, `spaces.get`, `journal.query`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `swarm`
- `peers`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | consolidated accounts |
| FORTRAN | trebago | forecasting |
| Ada | eve | risk appetite limits |
| APL | kron | KPI grid |
| MUMPS | myra | org chart tree |
| Python | python | AI |
| Delphi | - | structure |
| PHP | php | reports |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id culmen
matrix 305        # the graph's slot IS the matrix layer
name Culmen
needs ledger.read
needs swarm
needs peers
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

