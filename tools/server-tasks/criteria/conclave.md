# Criteria — conclave

| | |
|---|---|
| app | `conclave` |
| matrix layer | 369 |
| icon group | L3 |
| wave spec | `wave31/369-conclave.md` |

## Purpose

Runs your local AI swarm: choose models for this machine (`model.info`, `power.status`, `power.top`), set token budgets (`power.budget`, `swarm.state`), see each agent's work (`swarm.state`, `journal.query`) and accept or refuse its suggestions (`swarm.ask` answers held, `play.draft` untrusted). Changing the model slot and per-agent token budgets have no method beyond `power.budget`: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `ai.ask`
- `swarm`
- `sys.read`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | cycle ledger |
| FORTRAN | trebago | budget arithmetic |
| Ada | eve | budget limits |
| APL | kron | agent tensor tables |
| MUMPS | myra | agent records |
| Python | python | AI |
| Occam | - | concurrency |
| Simula 67 | - | simulation |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id conclave
matrix 369        # the graph's slot IS the matrix layer
name Conclave
needs ai.ask
needs swarm
needs sys.read
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

